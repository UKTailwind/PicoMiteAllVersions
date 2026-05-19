function(picomite_enable_bytecode_vm target)
    target_sources(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_alloc.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_bridge.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_source.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_compiler_core.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_vm.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_runtime.c
        ${CMAKE_SOURCE_DIR}/runtime/vm/bc_debug.c
    )
    target_compile_definitions(${target} PRIVATE PICOMITE_ENABLE_BYTECODE_VM=1)
    set_property(TARGET ${target} PROPERTY PICOMITE_BYTECODE_VM_ENABLED TRUE)
endfunction()

function(picomite_enable_bytecode_vm_pico_hooks target)
    target_sources(${target} PRIVATE
        ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/bc_crash_pico.c
        ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/bc_bridge_pico.c
        ${CMAKE_SOURCE_DIR}/ports/pico_sdk_common/bc_runtime_pico.c
    )
endfunction()

function(picomite_finalize_bytecode_vm target)
    get_property(_enabled TARGET ${target} PROPERTY PICOMITE_BYTECODE_VM_ENABLED)
    if(NOT _enabled)
        target_sources(${target} PRIVATE
            ${CMAKE_SOURCE_DIR}/runtime/vm/bc_unavailable.c
        )
    endif()
endfunction()
