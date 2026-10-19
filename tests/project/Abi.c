#include "LuiProject.h"

/* Compile the internal host interface as C, and call it from a C translation
   unit so language linkage/calling convention cannot silently drift. */
int LuiProjectCHeaderCheck(void) {
    LuiProjectSummaryV1 Summary = {0};
    Summary.StructSize = sizeof(Summary);
    Summary.AbiVersion = LUI_PROJECT_ABI_VERSION;
    Lui_ProjectRetain(0);
    Lui_ProjectRelease(0);
    return Lui_ProjectGetSummary(0, &Summary) == 0;
}
