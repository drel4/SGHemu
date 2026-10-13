/* Keep upstream puff unmodified; its checked small integers narrow explicitly
   by design. Suppress only this warning, only inside third-party source. */
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244)
#endif
#include "../third_party/puff/puff.c"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
