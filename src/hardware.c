#include "hardware.h"
#include "config.h"
#include "debug.h"

#include <psp2/kernel/sysmem.h>
#include <psp2/vshbridge.h>

static bool genuine_ps_tv;
static bool ps_tv_mode;

void vita_hardware_init(void) {
  int model = sceKernelGetModelForCDialog();
  ps_tv_mode = model == SCE_KERNEL_MODEL_VITATV;

  // The model is what the system runs as, which a plugin like MiniVitaTV
  // turns into a PS TV on a handheld, so pads can pair. The genuine check
  // reads the hardware itself. Linked weakly: if it can't be called, it
  // returns an error code, not 1, and the model decides.
  int genuine = vshSblAimgrIsGenuineDolce();
  genuine_ps_tv = genuine == 1 || (genuine != 0 && ps_tv_mode);

  vita_debug_log("[HARDWARE] model 0x%x, genuine PS TV %d (returned 0x%x), PS TV mode %d, force_ps_tv %d",
                 model, genuine_ps_tv, genuine, ps_tv_mode, config.force_ps_tv);
}

bool vita_is_ps_tv(void) {
  return genuine_ps_tv || config.force_ps_tv;
}

bool vita_ps_tv_mode(void) {
  return ps_tv_mode;
}

bool vita_has_battery(void) {
  return !vita_is_ps_tv();
}
