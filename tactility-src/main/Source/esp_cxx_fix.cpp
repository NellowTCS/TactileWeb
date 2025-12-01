extern "C" {

// Weak definition of __dso_handle to satisfy the linker for C++ global destructor support.
void* __dso_handle __attribute__((weak)) = (void*)0;

}