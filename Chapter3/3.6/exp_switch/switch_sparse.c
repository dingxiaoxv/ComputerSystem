/* 稀疏 case：观察编译器退化行为 */
long switch_sparse(long x) {
    long result = 0;
    switch (x) {
        case 1:    result = 10;  break;
        case 100:  result = 20;  break;
        case 1000: result = 30;  break;
        default:   result = -1;
    }
    return result;
}
