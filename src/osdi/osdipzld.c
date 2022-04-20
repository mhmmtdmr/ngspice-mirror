/* 
 * This file is part of the OSDI component of NGSPICE.
 * Copyright© 2022 SemiMod GmbH.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Pascal Kuthe <pascal.kuthe@semimod.de>
 */

#include "ngspice/iferrmsg.h"
#include "ngspice/memory.h"
#include "ngspice/ngspice.h"
#include "ngspice/typedefs.h"

#include "osdi.h"
#include "osdidefs.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int OSDIpzLoad(GENmodel *inModel, CKTcircuit *ckt, SPcomplex *s) {
  NG_IGNORE(ckt);

  GENmodel *gen_model;
  GENinstance *gen_inst;

  OsdiRegistryEntry *entry = osdi_reg_entry_model(inModel);
  const OsdiDescriptor *descr = entry->descriptor;
  for (gen_model = inModel; gen_model; gen_model = gen_model->GENnextModel) {
    void *model = osdi_model_data(gen_model);

    for (gen_inst = gen_model->GENinstances; gen_inst;
         gen_inst = gen_inst->GENnextInstance) {
      void *inst = osdi_instance_data(entry, gen_inst);
      // nothing to calculate just load the matrix entries calculated during
      // operating point iterations
      // the load_jacobian_tran function migh seem weird here but all this does
      // is adding J_resist + J_react * a to every matrix entry (real part).
      // J_resist are the conductances (normal matrix entries) and J_react the
      // capcitances
      descr->load_jacobian_tran(inst, model, s->real);
      descr->load_jacobian_react(inst, model, s->imag);
    }
  }
  return (OK);
}
