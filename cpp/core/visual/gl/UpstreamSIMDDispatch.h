#ifndef AETHER_UPSTREAM_VISUAL_SIMD_DISPATCH_H
#define AETHER_UPSTREAM_VISUAL_SIMD_DISPATCH_H

// Installs the byte-exact krkrz kernels that fill gaps in Aether's portable
// Highway implementation. The function is a no-op on unsupported targets.
void TVPInitUpstreamVisualSIMD();

#endif
