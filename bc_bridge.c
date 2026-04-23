/*
 * bc_bridge.c — Bridge between bytecode VM and interpreter command/function handlers.
 *
 * When the VM encounters an OP_BRIDGE_CMD, it has pre-tokenized bytes
 * (produced by tokenise() at compile time). This module sets up the
 * interpreter's global state (cmdtoken, cmdline, nextstmt) and calls
 * the appropriate command handler, then syncs variables back.
 */

#include <stdio.h>
#include <string.h>
#include "MMBasic.h"
#include "Memory.h"
#include "bytecode.h"
#include "bc_alloc.h"

/* Subfun-hash port hooks. rp2350 maintains a funtbl[] hash alongside
 * subfun[] so FindSubFun can resolve bridged SUB / FUNCTION names in
 * O(1). On rp2040 and host the hooks are no-ops (linear subfun[] scan
 * is fine at the smaller program sizes those targets run). Device
 * impl: ports/pico_sdk_common/bc_bridge_pico.c; host stub:
 * host/host_runtime.c. */
extern void port_bc_bridge_clear_subfun_hash(void);
extern void port_bc_bridge_rehash_subfun(unsigned char **subfun_arr);

/* ------------------------------------------------------------------ */
/*  Variable sync: VM globals <-> MMBasic variable table               */
/* ------------------------------------------------------------------ */

/*
 * Cached mapping from VM slot index -> g_vartbl index.
 * Built lazily on first bridge call and reused for the program's lifetime.
 * -1 means "not yet resolved".
 */
static int slot_to_vartbl[BC_MAX_SLOTS];
static int slot_map_initialized = 0;

void bc_bridge_reset_sync(void) {
    memset(slot_to_vartbl, -1, sizeof(slot_to_vartbl));
    slot_map_initialized = 0;
}

/*
 * Sync VM globals -> MMBasic variable table (pre-bridge-call).
 * Creates MMBasic variables if they don't exist yet.
 */
/* Build the findvar action flags that register a VM-side slot in
 * g_vartbl with the correct type. Pass T_IMPLIED so findvar marks the
 * entry as "declared-without-suffix" — a later bare-name lookup from
 * inside a bridged command handler (e.g. getint(p="W") in cmd_save)
 * passes its type check against DefaultType | T_IMPLIED instead of
 * erroring with "Different type already declared".
 *
 * Mirrors how the interpreter itself stores DIM-As-type variables:
 * `Dim W As Integer` stores W with type = T_INT | T_IMPLIED. */
static int sync_find_action(uint8_t slot_type) {
    int base = V_FIND | T_IMPLIED;
    switch (slot_type) {
        case T_INT: return base | T_INT;
        case T_NBR: return base | T_NBR;
        case T_STR: return base | T_STR;
        default:    return V_FIND;
    }
}

static void sync_vm_to_mmbasic(BCVMState *vm) {
    BCCompiler *cs = vm->compiler;
    if (!cs) return;

    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];

        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (vm->arrays[i].data || slot->is_array) continue;
        if (slot->type == T_STRUCT) continue;   // handled by the struct-only pass below

        unsigned char namebuf[MAXVARLEN + 2];
        int nlen = strlen(slot->name);
        memcpy(namebuf, slot->name, nlen);
        namebuf[nlen] = 0;

        if (!slot_map_initialized || slot_to_vartbl[i] < 0) {
            findvar(namebuf, sync_find_action(slot->type));
            slot_to_vartbl[i] = g_VarIndex;
        }

        int vi = slot_to_vartbl[i];
        struct s_vartbl *v = &g_vartbl[vi];

        /* Const-inlined globals (source_compile_const sets slot->is_const
         * and rolls back the runtime store) never touch vm->globals. Read
         * the value from the slot metadata instead, or the bridged
         * interpreter sees zero and bridged PRINT / DIM / expr eval all
         * break for any program that uses CONST + FRUN. */
        if (slot->is_const) {
            if (slot->type == T_INT) {
                v->val.i = slot->const_ival;
            } else if (slot->type == T_NBR) {
                v->val.f = slot->const_fval;
            }
            /* String consts go through vm->globals below — they aren't
             * rolled back because source_compile_const's literal-inline
             * fast path only covers OP_PUSH_INT / OP_PUSH_FLT. */
        }

        if (slot->type == T_INT) {
            if (!slot->is_const) v->val.i = vm->globals[i].i;
        } else if (slot->type == T_NBR) {
            if (!slot->is_const) v->val.f = vm->globals[i].f;
        } else if (slot->type == T_STR) {
            if (vm->globals[i].s) {
                v->val.s = GetTempMemory(STRINGSIZE);
                Mstrcpy(v->val.s, vm->globals[i].s);
            }
        }
    }

    /* Sync struct variables — resolve by name once, alias val.s into
     * vm->arrays[i].data so OP_LOAD/STORE_STRUCT_FIELD_* and the array-
     * element variants can reach the backing buffer.  For struct arrays we
     * also copy g_vartbl's dims[] into vm->arrays[i].dims so the element
     * opcodes can compute multi-dim linear indices without re-reading g_vartbl. */
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (slot->type != T_STRUCT) continue;

        /* Direct scan over g_vartbl by name — findvar requires a matching
         * subscript for arrays, which we don't have at this call site. */
        int vi = -1;
        int nlen = strlen(slot->name);
        for (int gv = 0; gv < MAXVARS; gv++) {
            if (g_vartbl[gv].name[0] == 0) continue;
            if ((g_vartbl[gv].type & T_STRUCT) == 0) continue;
            if (strncasecmp((char *)g_vartbl[gv].name, slot->name, nlen) != 0) continue;
            if (nlen < MAXVARLEN && g_vartbl[gv].name[nlen] != 0) continue;
            vi = gv;
            break;
        }
        if (vi < 0) continue;
        slot_to_vartbl[i] = vi;

        struct s_vartbl *v = &g_vartbl[vi];
        if (v->val.s != NULL) {
            vm->arrays[i].data = (BCValue *)v->val.s;
            vm->arrays[i].data_external = 1;
            if (slot->is_array && v->dims[0] != 0) {
                int nd = 0;
                for (int d = 0; d < MAXDIM && v->dims[d] != 0; d++) nd++;
                vm->arrays[i].ndims = (uint8_t)nd;
                for (int d = 0; d < MAXDIM; d++) {
                    vm->arrays[i].dims[d] = v->dims[d];
                }
            }
        }
    }

    /* Sync arrays — point MMBasic's array data to VM's array data */
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (!slot->is_array) continue;
        if (!vm->arrays[i].data) continue;

        unsigned char namebuf[MAXVARLEN + 4];
        int nlen = strlen(slot->name);
        memcpy(namebuf, slot->name, nlen);
        namebuf[nlen] = 0;

        BCArray *arr = &vm->arrays[i];
        int action = sync_find_action(slot->type);

        if (!slot_map_initialized || slot_to_vartbl[i] < 0) {
            namebuf[nlen] = '(';
            namebuf[nlen + 1] = ')';
            namebuf[nlen + 2] = 0;
            if (findvar(namebuf, action | V_EMPTY_OK | V_NOFIND_NULL) != NULL) {
                slot_to_vartbl[i] = g_VarIndex;
            } else {
                namebuf[nlen] = 0;
                findvar(namebuf, action);
                slot_to_vartbl[i] = g_VarIndex;
            }

            struct s_vartbl *v = &g_vartbl[slot_to_vartbl[i]];
            for (int d = 0; d < MAXDIM; d++) {
                v->dims[d] = (d < arr->ndims) ? arr->dims[d] : 0;
            }
        }

        int vi = slot_to_vartbl[i];
        struct s_vartbl *v = &g_vartbl[vi];

        if (slot->type == T_INT) {
            v->val.ia = (long long int *)arr->data;
        } else if (slot->type == T_NBR) {
            v->val.fa = (MMFLOAT *)arr->data;
        } else if (slot->type == T_STR) {
            v->val.s = (unsigned char *)arr->data;
        }
    }

    slot_map_initialized = 1;
}

/*
 * Sync MMBasic variable table -> VM globals (post-bridge-call).
 */
static void sync_mmbasic_to_vm(BCVMState *vm) {
    BCCompiler *cs = vm->compiler;
    if (!cs || !slot_map_initialized) return;

    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (vm->arrays[i].data) continue;  /* arrays handled separately below */
        if (slot_to_vartbl[i] < 0) continue;

        struct s_vartbl *v = &g_vartbl[slot_to_vartbl[i]];

        if (slot->type == T_INT) {
            vm->globals[i].i = v->val.i;
        } else if (slot->type == T_NBR) {
            vm->globals[i].f = v->val.f;
        } else if (slot->type == T_STR) {
            if (v->val.s) {
                if (!vm->globals[i].s) {
                    vm->globals[i].s = BC_ALLOC(STRINGSIZE);
                }
                if (vm->globals[i].s) {
                    Mstrcpy(vm->globals[i].s, v->val.s);
                }
            }
        }
    }

    /* Phase 13: first-time adoption for bridged int/float DIMs.  When a
     * DIM allocates a non-struct non-string sized array via the bridge
     * (because fe->uses_struct_extract_insert forced it), vm->arrays[i].data
     * starts NULL.  Walk g_vartbl directly, look up by suffix-stripped name,
     * and adopt the pointer so subsequent OP_LOAD/STORE_ARR_* opcodes see
     * the contiguous g_vartbl buffer.
     *
     * String arrays (T_STR) are intentionally excluded: the VM stores them
     * as `BCValue[] → 256-byte buffer per element`, which is incompatible
     * with g_vartbl's contiguous layout.  Programs that do
     * `STRUCT EXTRACT arr().member, dest$()` / `INSERT src$(), arr().member`
     * therefore only run correctly under `--interp` for the string case;
     * the compare-mode test skips those paths explicitly. */
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (!slot->is_array) continue;
        if (vm->arrays[i].data) continue;
        if (slot->type == T_STRUCT || slot->type == T_STR) continue;

        /* Strip trailing type suffix — g_vartbl stores names without it. */
        int nlen = (int)strlen(slot->name);
        if (nlen > 0) {
            char last = slot->name[nlen - 1];
            if (last == '$' || last == '%' || last == '!') nlen--;
        }
        int vi = -1;
        for (int gv = 0; gv < MAXVARS; gv++) {
            if (g_vartbl[gv].name[0] == 0) continue;
            if ((g_vartbl[gv].type & slot->type) == 0) continue;
            if (g_vartbl[gv].dims[0] == 0) continue;
            if (strncasecmp((char *)g_vartbl[gv].name, slot->name, nlen) != 0) continue;
            if (nlen < MAXVARLEN && g_vartbl[gv].name[nlen] != 0) continue;
            vi = gv;
            break;
        }
        if (vi < 0) continue;

        struct s_vartbl *v = &g_vartbl[vi];
        void *ptr = (slot->type == T_INT) ? (void *)v->val.ia :
                    (slot->type == T_NBR) ? (void *)v->val.fa : NULL;
        if (!ptr) continue;

        slot_to_vartbl[i] = vi;
        BCArray *arr = &vm->arrays[i];
        arr->data = (BCValue *)ptr;
        arr->data_external = 1;

        int ndims = 0;
        uint32_t total = 1;
        for (int d = 0; d < MAXDIM; d++) {
            int sz = v->dims[d];
            arr->dims[d] = sz;
            if (sz > 0) {
                total *= (uint32_t)(sz + 1);
                ndims = d + 1;
            }
        }
        arr->ndims = (uint8_t)ndims;
        arr->total_elements = total;
    }

    /* Array rebinding pass.
     *
     * The pre-bridge sync (sync_vm_to_mmbasic) aliases g_vartbl[vi].val.ia/fa/s
     * to vm->arrays[i].data, which works for every bridged command that
     * mutates array contents in place.  REDIM (and any future command that
     * reallocates an array's backing buffer) breaks that contract: the
     * interpreter frees the old buffer, allocates a new one in g_vartbl,
     * and may shuffle the variable's slot index (redim_erase_var clears
     * the original slot).  After the bridge returns we re-resolve the
     * vartbl entry by name, detect a changed pointer, and adopt it into
     * vm->arrays[i] so subsequent OP_LOAD_ARR_* / OP_STORE_ARR_* opcodes
     * read the fresh buffer.  The adopted buffer is owned by g_vartbl, so
     * we flag it data_external = 1 to prevent bc_array_release from
     * bc_free-ing interpreter-owned memory at program teardown. */
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (!vm->arrays[i].data) continue;
        if (slot->type == T_STRUCT) continue;   // struct rebinding pass handles these

        unsigned char namebuf[MAXVARLEN + 4];
        int nlen = strlen(slot->name);
        memcpy(namebuf, slot->name, nlen);

        int action = sync_find_action(slot->type);
        namebuf[nlen] = '(';
        namebuf[nlen + 1] = ')';
        namebuf[nlen + 2] = 0;
        void *vp = findvar(namebuf, action | V_EMPTY_OK | V_NOFIND_NULL);
        if (vp == NULL) {
            namebuf[nlen] = 0;
            vp = findvar(namebuf, action | V_NOFIND_NULL);
        }
        if (vp == NULL) continue;  /* variable was erased — leave VM array as-is */
        slot_to_vartbl[i] = g_VarIndex;

        struct s_vartbl *v = &g_vartbl[slot_to_vartbl[i]];
        void *new_ptr = (slot->type == T_INT) ? (void *)v->val.ia :
                        (slot->type == T_NBR) ? (void *)v->val.fa :
                                                (void *)v->val.s;

        BCArray *arr = &vm->arrays[i];

        /* Refresh dims and total_elements from the interpreter's view —
         * always, not just on pointer change. REDIM can return the same
         * pointer when the heap allocator reuses the freed slot (small
         * programs hit this on nearly-empty heaps). Skipping the refresh
         * on the pointer-unchanged fast path would leave stale dims and
         * the bounds check rejects the newly-valid REDIM'd index. */
        if (new_ptr != arr->data) {
            arr->data = (BCValue *)new_ptr;
            arr->data_external = 1;
        }

        int ndims = 0;
        uint32_t total = 1;
        for (int d = 0; d < MAXDIM; d++) {
            int size = v->dims[d];
            arr->dims[d] = size;
            if (size > 0) {
                total *= (uint32_t)(size + 1);  /* 0..N inclusive, matches OP_DIM_ARR_* */
                ndims = d + 1;
            }
        }
        arr->ndims = (uint8_t)ndims;
        arr->total_elements = total;
    }

    /* Struct rebinding pass — resolve T_STRUCT slots by name (possibly for the
     * first time after cmd_dim allocates them) and cache val.s into
     * vm->arrays[i].data so the field-access opcodes can reach the buffer
     * without re-calling findvar on every access.  Array dims also flow back
     * so multi-dim element indexing can use vm->arrays[i].dims. */
    for (uint16_t i = 0; i < cs->slot_count; i++) {
        BCSlot *slot = &cs->slots[i];
        if (!slot->name[0] || !isnamestart((unsigned char)slot->name[0])) continue;
        if (slot->type != T_STRUCT) continue;

        int vi = -1;
        int nlen = strlen(slot->name);
        for (int gv = 0; gv < MAXVARS; gv++) {
            if (g_vartbl[gv].name[0] == 0) continue;
            if ((g_vartbl[gv].type & T_STRUCT) == 0) continue;
            if (strncasecmp((char *)g_vartbl[gv].name, slot->name, nlen) != 0) continue;
            if (nlen < MAXVARLEN && g_vartbl[gv].name[nlen] != 0) continue;
            vi = gv;
            break;
        }
        if (vi < 0) continue;
        slot_to_vartbl[i] = vi;

        struct s_vartbl *v = &g_vartbl[vi];
        if (v->val.s == NULL) continue;

        if (slot->is_array && v->dims[0] != 0) {
            int nd = 0;
            for (int d = 0; d < MAXDIM && v->dims[d] != 0; d++) nd++;
            vm->arrays[i].ndims = (uint8_t)nd;
            for (int d = 0; d < MAXDIM; d++) {
                vm->arrays[i].dims[d] = v->dims[d];
            }
        }

        if (vm->arrays[i].data == (BCValue *)v->val.s) continue;   // unchanged
        vm->arrays[i].data = (BCValue *)v->val.s;
        vm->arrays[i].data_external = 1;
    }
}

/*
 * Sync VM locals -> MMBasic local variables (pre-bridge-call).
 * Returns the local scope level (for ClearVars cleanup).
 */
static int sync_vm_locals_to_mmbasic(BCVMState *vm, int *local_map) {
    BCCompiler *cs = vm->compiler;
    if (!cs || vm->csp <= 0) return 0;

    BCCallFrame *cf = &vm->call_stack[vm->csp - 1];
    if (cf->subfun_idx >= cs->subfun_count) return 0;

    BCSubFun *sf = &cs->subfuns[cf->subfun_idx];
    uint16_t locals_base = cs->subfun_locals_base[cf->subfun_idx];
    if (sf->nlocals == 0) return 0;

    g_LocalIndex++;
    for (int i = 0; i < VM_MAX_LOCALS; i++) local_map[i] = -1;

    for (uint16_t i = 0; i < sf->nlocals; i++) {
        int slot = vm->frame_base + i;
        if (slot < 0 || slot >= VM_MAX_LOCALS) continue;

        BCLocalMeta *meta = &cs->local_meta[locals_base + i];
        if (!meta->name[0] || !isnamestart((unsigned char)meta->name[0])) continue;

        unsigned char namebuf[MAXVARLEN + 4];
        int nlen = strlen(meta->name);
        memcpy(namebuf, meta->name, nlen);
        namebuf[nlen] = 0;

        int action = V_FIND | V_DIM_VAR | V_LOCAL | V_EMPTY_OK | meta->type;
        if (sf->return_type != 0 && i == 0) {
            action |= V_FUNCT;
        }

        if (meta->is_array) {
            BCArray *arr = &vm->local_arrays[slot];
            namebuf[nlen] = '(';
            namebuf[nlen + 1] = ')';
            namebuf[nlen + 2] = 0;
            findvar(namebuf, action);
            local_map[slot] = g_VarIndex;

            if (arr->data) {
                struct s_vartbl *v = &g_vartbl[g_VarIndex];
                for (int d = 0; d < MAXDIM; d++) {
                    v->dims[d] = (d < arr->ndims) ? arr->dims[d] : 0;
                }
                if (meta->type == T_INT) v->val.ia = (long long int *)arr->data;
                else if (meta->type == T_NBR) v->val.fa = (MMFLOAT *)arr->data;
                else if (meta->type == T_STR) v->val.s = (unsigned char *)arr->data;
            }
            continue;
        }

        findvar(namebuf, action);
        local_map[slot] = g_VarIndex;
        struct s_vartbl *v = &g_vartbl[g_VarIndex];
        if (meta->type == T_INT) {
            v->val.i = vm->locals[slot].i;
        } else if (meta->type == T_NBR) {
            v->val.f = vm->locals[slot].f;
        } else if (meta->type == T_STR && vm->locals[slot].s) {
            v->val.s = GetTempMemory(STRINGSIZE);
            Mstrcpy(v->val.s, vm->locals[slot].s);
        }
    }

    return g_LocalIndex;
}

/*
 * Sync MMBasic locals -> VM locals (post-bridge-call).
 */
static void sync_mmbasic_locals_to_vm(BCVMState *vm, const int *local_map) {
    BCCompiler *cs = vm->compiler;
    if (!cs || vm->csp <= 0) return;

    BCCallFrame *cf = &vm->call_stack[vm->csp - 1];
    if (cf->subfun_idx >= cs->subfun_count) return;

    BCSubFun *sf = &cs->subfuns[cf->subfun_idx];
    uint16_t locals_base = cs->subfun_locals_base[cf->subfun_idx];
    for (uint16_t i = 0; i < sf->nlocals; i++) {
        int slot = vm->frame_base + i;
        if (slot < 0 || slot >= VM_MAX_LOCALS) continue;
        if (local_map[slot] < 0) continue;

        BCLocalMeta *meta = &cs->local_meta[locals_base + i];
        if (meta->is_array) continue;

        struct s_vartbl *v = &g_vartbl[local_map[slot]];
        vm->local_types[slot] = meta->type;
        if (meta->type == T_INT) {
            vm->locals[slot].i = v->val.i;
        } else if (meta->type == T_NBR) {
            vm->locals[slot].f = v->val.f;
        } else if (meta->type == T_STR && v->val.s) {
            uint8_t *temp = &vm->str_temp[vm->str_temp_idx % 4][0];
            vm->str_temp_idx++;
            Mstrcpy(temp, v->val.s);
            vm->locals[slot].s = temp;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Bridge dispatch                                                    */
/* ------------------------------------------------------------------ */

/*
 * Decode a 2-byte command token to get the commandtbl index.
 */
static inline int bridge_decode_cmd(const uint8_t *p) {
    return (p[0] & 0x7f) | ((p[1] & 0x7f) << 7);
}

/*
 * bc_bridge_call_cmd — Execute a bridged command.
 *
 * tok points to the tokenized bytes (command token + args).
 * len is the total length of the tokenized data.
 *
 * The tokenized form starts with a 2-byte command token, followed by
 * the tokenized arguments, terminated by 0x00.
 */
void bc_bridge_call_cmd(BCVMState *vm, const uint8_t *tok, uint16_t len) {
    if (len < 2) return;

    /* Copy tokenized bytes to temp memory — command handlers may modify
     * the buffer during parsing. */
    unsigned char *buf = GetTempMemory(len + 1);
    memcpy(buf, tok, len);
    buf[len] = 0;

    /* Classify the first two bytes the same way the interpreter's ExecuteProgram
     * line-loop does: two command-token bytes (high bit set on both) dispatch
     * via commandtbl; otherwise try FindSubFun for a user-defined SUB call. */
    int is_cmd = (buf[0] >= C_BASETOKEN && buf[1] >= C_BASETOKEN);

    /* Save interpreter context */
    int saved_cmdtoken = cmdtoken;
    unsigned char *saved_cmdline = cmdline;
    unsigned char *saved_nextstmt = nextstmt;
    unsigned char *saved_current_line = CurrentLinePtr;
    int saved_local_index = g_LocalIndex;
    int local_map[VM_MAX_LOCALS];

    /* Pretend we're inside a running program rather than at the REPL.
     * Several interp commands (cmd_files, cmd_load, cmd_new, …) branch
     * on `CurrentLinePtr == NULL` and call ClearRuntime / ClearVars
     * which wipes the entire MMHeap. With shared bc_alloc + MMHeap
     * (matching device), that wipe also frees the live VMState mid-
     * dispatch and the next opcode segfaults. Setting a non-NULL
     * sentinel here puts those commands on their program-driven path. */
    if (!CurrentLinePtr) CurrentLinePtr = buf;
    int bridge_level = 0;

    /* Sync VM variables to MMBasic table */
    sync_vm_to_mmbasic(vm);
    bridge_level = sync_vm_locals_to_mmbasic(vm, local_map);

    if (is_cmd) {
        int cmd_idx = bridge_decode_cmd(buf);
        if (cmd_idx < 0 || cmd_idx >= CommandTableSize) {
            bc_vm_error(vm, "Bridge: invalid command token %d", cmd_idx);
            return;
        }
        cmdtoken = cmd_idx;
        cmdline = buf + 2;
        nextstmt = buf + len;
        commandtbl[cmd_idx].fptr();
    } else {
        /* User-defined SUB call — e.g. `MovePoint p, 5, 10` where MovePoint
         * takes a struct param (Phase 7) so the VM compiler bridged it.
         *
         * DefinedSubFun for SUBs is non-transitional: it pushes the caller's
         * `nextstmt` onto gosubstack and sets nextstmt to the sub body, then
         * returns.  The interpreter's main ExecuteProgram loop would normally
         * drive the body; in the bridge we do that ourselves.  A double-NUL
         * sentinel makes ExecuteProgram exit when `End Sub` / `Exit Sub`
         * pops us back. */
        if (!isnamestart((unsigned char)buf[0])) {
            bc_vm_error(vm, "Bridge: invalid statement '%s'", (char *)buf);
            return;
        }
        int idx = FindSubFun(buf, 0);
        if (idx < 0) {
            bc_vm_error(vm, "Bridge: unknown SUB '%s'", (char *)buf);
            return;
        }
        static unsigned char sub_ret_sentinel[4] = {0, 0, 0, 0};
        unsigned char *pre_nextstmt = nextstmt;
        cmdline = buf;
        nextstmt = sub_ret_sentinel;
        DefinedSubFun(false, buf, idx, NULL, NULL, NULL, NULL);
        if (nextstmt != sub_ret_sentinel) {
            /* DefinedSubFun set nextstmt to the sub body — drive the body via
             * a scoped ExecuteProgram.  cmd_return (invoked by End Sub) will
             * pop gosubstack back to sub_ret_sentinel, the loop exits. */
            ExecuteProgram(nextstmt);
        }
        nextstmt = pre_nextstmt;
    }

    /* Sync modified variables back to VM */
    sync_mmbasic_locals_to_vm(vm, local_map);
    sync_mmbasic_to_vm(vm);
    if (bridge_level) ClearVars(bridge_level, 1);
    g_LocalIndex = saved_local_index;

    /* Restore interpreter context */
    cmdtoken = saved_cmdtoken;
    cmdline = saved_cmdline;
    nextstmt = saved_nextstmt;
    CurrentLinePtr = saved_current_line;

    ClearTempMemory();
}

/*
 * bc_bridge_call_fun — Execute a bridged function.
 *
 * fun_idx is the tokentbl index of the function.
 * args points to pre-tokenized argument bytes (what would go in ep).
 * arg_len is the length of the argument bytes (0 for T_FNA no-arg functions).
 * ret_type is the expected return type (T_INT, T_NBR, or T_STR).
 *
 * Sets up the interpreter's function-call globals (ep, targ) and calls
 * the function handler, then pushes the result onto the VM stack.
 */
void bc_bridge_call_fun(BCVMState *vm, uint16_t fun_idx, const uint8_t *args, uint16_t arg_len, uint8_t ret_type) {
    if (fun_idx >= (unsigned)TokenTableSize - 1) {
        bc_vm_error(vm, "Bridge: invalid function index %d", fun_idx);
        return;
    }

    /* Save interpreter context */
    int saved_targ = targ;
    unsigned char *saved_ep = ep;
    MMFLOAT saved_fret = fret;
    long long int saved_iret = iret;
    unsigned char *saved_sret = sret;
    int saved_local_index = g_LocalIndex;
    int local_map[VM_MAX_LOCALS];
    int bridge_level = 0;

    /* Sync VM variables to MMBasic table */
    sync_vm_to_mmbasic(vm);
    bridge_level = sync_vm_locals_to_mmbasic(vm, local_map);

    /* Set up function arguments in ep */
    if (arg_len > 0) {
        ep = GetTempMemory(STRINGSIZE);
        memcpy(ep, args, arg_len);
        ep[arg_len] = 0;
    } else {
        ep = GetTempMemory(1);
        ep[0] = 0;
    }

    targ = ret_type;
    tokentbl[fun_idx].fptr();

    /* Capture result before cleanup */
    MMFLOAT result_f = fret;
    long long int result_i = iret;
    unsigned char *result_s = sret;

    /* Sync modified variables back to VM */
    sync_mmbasic_locals_to_vm(vm, local_map);
    sync_mmbasic_to_vm(vm);
    if (bridge_level) ClearVars(bridge_level, 1);
    g_LocalIndex = saved_local_index;

    /* Push result onto VM stack */
    if (ret_type == T_INT) {
        if (vm->sp + 1 >= VM_STACK_SIZE) { bc_vm_error(vm, "Stack overflow"); goto cleanup; }
        vm->sp++;
        vm->stack[vm->sp].i = result_i;
        vm->stack_types[vm->sp] = T_INT;
    } else if (ret_type == T_NBR) {
        if (vm->sp + 1 >= VM_STACK_SIZE) { bc_vm_error(vm, "Stack overflow"); goto cleanup; }
        vm->sp++;
        vm->stack[vm->sp].f = result_f;
        vm->stack_types[vm->sp] = T_NBR;
    } else if (ret_type == T_STR) {
        /* Copy string to VM temp storage before ClearTempMemory */
        uint8_t *temp = &vm->str_temp[vm->str_temp_idx & 3][0];
        vm->str_temp_idx = (vm->str_temp_idx + 1) & 3;
        if (result_s) {
            int slen = *result_s;
            memcpy(temp, result_s, slen + 1);
        } else {
            temp[0] = 0;
        }
        if (vm->sp + 1 >= VM_STACK_SIZE) { bc_vm_error(vm, "Stack overflow"); goto cleanup; }
        vm->sp++;
        vm->stack[vm->sp].s = temp;
        vm->stack_types[vm->sp] = T_STR;
    }

cleanup:
    /* Restore interpreter context */
    targ = saved_targ;
    ep = saved_ep;
    fret = saved_fret;
    iret = saved_iret;
    sret = saved_sret;

    ClearTempMemory();
}

/* ------------------------------------------------------------------ */
/*  Bridge subfun[] population                                         */
/* ------------------------------------------------------------------ */

/*
 * When the VM bridges back to the interpreter (bc_bridge_call_cmd /
 * bc_bridge_call_fun) and the bridged statement's expression references
 * a user-defined SUB or FUNCTION, the interpreter's expression evaluator
 * calls FindSubFun() — which walks subfun[] (and funtbl[] on rp2350).
 *
 * bc_run_source_string_ex compiles the program directly to VM bytecode
 * without routing through PrepareProgram, so subfun[] stays empty and
 * the bridged name lookup fails — user-function calls inside bridged
 * args surface as "Dimensions" or "Unknown command" errors.
 *
 * bc_bridge_prepare_subfun solves this by tokenising the raw source
 * into a RAM side-buffer and then scanning that buffer for SUB / FUN /
 * CSUB tokens, wiring subfun[] entries to point into our buffer. No
 * flash writes on device; on host we bypass ProgMemory entirely so we
 * don't disturb whatever the interpreter-oracle comparison loaded.
 *
 * bc_bridge_release_subfun_buffer frees the buffer and nulls subfun[].
 */

static unsigned char *g_bridge_prog_buf = NULL;

unsigned char *bc_bridge_get_prog_buf(void) {
    return g_bridge_prog_buf;
}

void bc_bridge_release_subfun_buffer(void) {
    if (g_bridge_prog_buf) {
        BC_FREE(g_bridge_prog_buf);
        g_bridge_prog_buf = NULL;
    }
    for (int i = 0; i < MAXSUBFUN; i++) subfun[i] = NULL;
    port_bc_bridge_clear_subfun_hash();
}

int bc_bridge_prepare_subfun(const char *source) {
    if (!source || !*source) {
        bc_bridge_release_subfun_buffer();
        return 0;
    }

    /* Tokenised form is typically <= source length. Add headroom for
     * per-line terminators + 0xff end-of-program sentinel.
     *
     * Allocate FIRST, then release the old buffer + clobber subfun[].
     * Releasing first would wipe the prior PrepareProgram state on device
     * — and if the BC_ALLOC then fails (rp2040 with combined bc_alloc /
     * MMHeap pressure) the bridged-call lookup would see an empty
     * subfun[] instead of the still-correct prior table. Symptom on
     * device was "Bridge: unknown SUB <name>" after RUN-then-FRUN.
     *
     * Filtering to SUB/FUNCTION/CSUB-only blocks would cut this in half
     * but breaks findlabel (bc_runtime.c points ProgMemory at this
     * buffer during bridged execution, and label refs like TILEMAP
     * CREATE <label> / RESTORE <label> / GOTO <label> need the full
     * program). Keep the whole program tokenised. */
    size_t src_len = strlen(source);
    size_t cap = src_len + 64;
    unsigned char *buf = (unsigned char *)BC_ALLOC(cap);
    if (!buf) return -1;
    bc_bridge_release_subfun_buffer();
    /* 0xff fill so the tail acts as the end-of-program sentinel that
     * the standard MMBasic walker (and our scan below) look for. */
    memset(buf, 0xff, cap);

    /* Save caller's line-buffer state — tokenise() writes tknbuf/inpbuf. */
    unsigned char saved_inpbuf[STRINGSIZE];
    unsigned char saved_tknbuf[STRINGSIZE];
    memcpy(saved_inpbuf, inpbuf, STRINGSIZE);
    memcpy(saved_tknbuf, tknbuf, STRINGSIZE);

    unsigned char *pm = buf;
    const char *line = source;
    while (*line) {
        const char *eol = strchr(line, '\n');
        size_t len = eol ? (size_t)(eol - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len >= STRINGSIZE) len = STRINGSIZE - 1;

        if (len > 0) {
            memcpy(inpbuf, line, len);
            inpbuf[len] = '\0';
            tokenise(0);

            /* tokenise() terminates tknbuf with two zero bytes. T_LINENBR
             * embeds single zero bytes, so walk until two consecutive
             * zeros (same pattern used by SaveProgramToFlash). */
            unsigned char *tp = tknbuf;
            while (!(tp[0] == 0 && tp[1] == 0)) {
                if ((size_t)(pm - buf) >= cap - 3) goto buf_full;
                *pm++ = *tp++;
            }
            *pm++ = 0;  /* element terminator */
        }
        line = eol ? eol + 1 : line + strlen(line);
    }
buf_full:
    *pm++ = 0;  /* program terminator byte 1 */
    *pm++ = 0;  /* program terminator byte 2 */

    memcpy(inpbuf, saved_inpbuf, STRINGSIZE);
    memcpy(tknbuf, saved_tknbuf, STRINGSIZE);

    /* Scan tokenised buffer for SUB/FUN/CSUB tokens and populate
     * subfun[]. Mirrors PrepareProgramExt's core walk without its
     * ErrAbort / CFunction-scanning paths. */
    for (int i = 0; i < MAXSUBFUN; i++) subfun[i] = NULL;
    int si = 0;
    unsigned char *p = buf;
    while (*p != 0xff && si < MAXSUBFUN) {
        /* Skip leading whitespace / label prefix. The interpreter walks
         * this via GetNextCommand; for a pristine tokeniser output stream
         * skipspace is enough. */
        while (*p == ' ' || *p == T_NEWLINE || *p == T_LINENBR) {
            if (*p == T_LINENBR) { p += 3; continue; }
            p++;
        }
        if (*p == 0) {
            p++;
            if (*p == 0) break;  /* double-zero = end-of-program */
            continue;
        }
        /* Inline commandtbl_decode — it's static inline in MMBasic.c and
         * not exported through MMBasic.h. */
        CommandToken tkn = (CommandToken)((p[0] & 0x7f) | ((p[1] & 0x7f) << 7));
        if (tkn == cmdSUB || tkn == cmdFUN || tkn == cmdCSUB) {
            subfun[si++] = p;
        }
        while (*p) p++;  /* skip to end-of-element */
    }

    /* Rebuild the rp2350-only funtbl[] hash for FindSubFun's fast
     * path. Rp2040 + host use the linear subfun[] scan, so the hook
     * is a no-op there. */
    port_bc_bridge_rehash_subfun(subfun);

    g_bridge_prog_buf = buf;
    return 0;
}
