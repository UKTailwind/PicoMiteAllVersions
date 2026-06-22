__attribute__((noinline)) static int triple(int x){ return x + x + x; }
long long scale3(long long *a){ *a = (long long)triple((int)*a); return 0; }
