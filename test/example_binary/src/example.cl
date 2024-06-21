kernel void vecaccum(global int*A, global int*B) {
   size_t gid = get_global_id(0);
   A[gid] += B[gid];
};
kernel void vecsum(global int*A, constant int*B, constant int*C) {
   size_t gid = get_global_id(0);
   A[gid] = B[gid] + C[gid];
};
kernel void printit(global int*A) {
   printf("Hello world! %d\n", A[0]);
};
