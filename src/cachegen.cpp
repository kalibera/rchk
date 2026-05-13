#include "cache.h"

int main(int argc, char* argv[]) {
  if (argc != 3) {
    errs() << "Usage: cachegen <base_ir_file> <output_cache_file>\n";
    return 1;
  }

  return createRCacheFile(argv[1], argv[2]) ? 0 : 1;
}
