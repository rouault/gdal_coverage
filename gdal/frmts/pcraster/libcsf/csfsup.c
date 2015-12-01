#include "csf.h"
#include "csftypes.h"
#include <string.h>    /* memset */

/* set an array of cells to missing value
 * SetMemMV sets an array of cells to missing value
 */
void SetMemMV(
	void *buf,		/* write-only buffer with cells */
	size_t nrElements,      /* number of cells */
	CSF_CR cellRepr)         /* cell representation */
{
size_t index;

	switch (cellRepr) {
	  case CR_INT1: (void)memset(buf,MV_INT1+256,nrElements);break;
	  case CR_INT2: for (index=0;index<nrElements;index++)
					 ((INT2 *) buf)[index]=MV_INT2;
			break;
	  case CR_INT4: for (index=0;index<nrElements;index++)
					 ((INT4 *) buf)[index]=MV_INT4;
			break;
	  default: (void)memset(buf,MV_UINT1,CSFSIZEOF(nrElements,cellRepr));
	}
}
