#pragma once
// Keeps the game off the E-cores of a hybrid CPU and raises its priority class.
//
// Two engine views double the CPU-side render work, and upstream's profiling has the game's
// main thread at ~97% of one logical CPU with the GPU under 90%. On a 12700K-class part the
// scheduler will happily put that thread on an E-core, which is a visible frame-time hit.
// xr_cpu_affinity: 0 = leave the process alone; 1 = P-cores only (hyperthreads included) and
// ABOVE_NORMAL priority; 2 = same with HIGH priority. Applied once per distinct value, so a
// live change from vrport.ini takes effect without a restart. No-op on a CPU without
// efficiency classes.
namespace cvr::cpu {
void Apply(int mode);
const char* Status();
}
