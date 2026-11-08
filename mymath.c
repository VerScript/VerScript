#include <windows.h>

__declspec(dllexport) int add(int a, int b) {
    return a + b;
}

__declspec(dllexport) int fast_multiply(int x, int factor) {
    return x * factor;
}

__declspec(dllexport) int hypot_dist(int x, int y) {
    return (int)(x * x + y * y);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    return TRUE;
}
