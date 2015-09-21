/* flags.c */

#include "include/child.h"
#include "child_private.h"

_CHILDIMP int __cdecl child_flags(HCHILD hcl)
{
	return hcl->iFlags;
}
