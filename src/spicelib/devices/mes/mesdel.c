/**********
Copyright 1990 Regents of the University of California.  All rights reserved.
Author: 1985 S. Hwang
**********/

#include "ngspice/ngspice.h"
#include "mesdefs.h"
#include "ngspice/sperror.h"
#include "ngspice/suffix.h"


int
MESdelete(GENmodel *model, IFuid name, GENinstance **kill)
{
    for (; model; model = model->GENnextModel) {
        GENinstance **prev = &(model->GENinstances);
        GENinstance *here = *prev;
        for (; here; here = *prev) {
            if (here->GENname == name || (kill && here == *kill)) {
                *prev = here->GENnextInstance;
                FREE(here);
                return OK;
            }
            prev = &(here->GENnextInstance);
        }
    }

    return E_NODEV;
}
