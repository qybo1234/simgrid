/* Copyright (c) 2014-2019. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "src/include/catch.hpp"

#include <cstdlib>
#include <cstring>
#include <random>

#include <sys/mman.h>

#include "src/mc/mc_config.hpp"
#include "src/mc/mc_mmu.hpp"
#include "src/mc/mc_private.hpp"
#include "src/mc/sosp/mc_snapshot.hpp"

/**************** Class BOOST_tests *************************/
using simgrid::mc::RegionSnapshot;
class snap_test_helper {
public:
  static void init_memory(void* mem, size_t size);
  static void Init(bool sparse_ckpt);
  typedef struct {
    size_t size;
    void* src;
    void* dstn;
    RegionSnapshot region0;
    RegionSnapshot region;
  } prologue_return;
  static prologue_return prologue(int n); // common to the below 5 fxs
  static void read_whole_region();
  static void read_region_parts();
  static void compare_whole_region();
  static void compare_region_parts();
  static void read_pointer();

  static void cleanup()
  {
    delete mc_model_checker;
    mc_model_checker = nullptr;
  }

  static std::default_random_engine rnd_engine;
  static bool sparse_checkpoint;
  static std::unique_ptr<simgrid::mc::RemoteClient> process;
};

// static member variables init.
std::default_random_engine snap_test_helper::rnd_engine;
bool snap_test_helper::sparse_checkpoint                             = 0;
std::unique_ptr<simgrid::mc::RemoteClient> snap_test_helper::process = nullptr;

void snap_test_helper::init_memory(void* mem, size_t size)
{
  char* dest = (char*)mem;
  for (size_t i = 0; i < size; ++i) {
    dest[i] = rnd_engine() & 255;
  }
}

void snap_test_helper::Init(bool sparse_ckpt)
{
  _sg_mc_sparse_checkpoint = sparse_ckpt;
  REQUIRE(xbt_pagesize == getpagesize());
  REQUIRE(1 << xbt_pagebits == xbt_pagesize);

  process.reset(new simgrid::mc::RemoteClient(getpid(), -1));
  process->init();
  mc_model_checker = new ::simgrid::mc::ModelChecker(std::move(process));
}

snap_test_helper::prologue_return snap_test_helper::prologue(int n)
{
  // Store region page(s):
  size_t byte_size = n * xbt_pagesize;
  void* source     = mmap(nullptr, byte_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  INFO("Could not allocate source memory")
  REQUIRE(source != MAP_FAILED);

  // Init memory and take snapshots:
  init_memory(source, byte_size);
  simgrid::mc::RegionSnapshot region0 =
      simgrid::mc::sparse_region(simgrid::mc::RegionType::Unknown, source, source, byte_size);
  for (int i = 0; i < n; i += 2) {
    init_memory((char*)source + i * xbt_pagesize, xbt_pagesize);
  }
  simgrid::mc::RegionSnapshot region =
      simgrid::mc::sparse_region(simgrid::mc::RegionType::Unknown, source, source, byte_size);

  void* destination = mmap(nullptr, byte_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  INFO("Could not allocate destination memory");
  REQUIRE(source != MAP_FAILED);

  return {.size    = byte_size,
          .src     = source,
          .dstn    = destination,
          .region0 = std::move(region0),
          .region  = std::move(region)};
}

void snap_test_helper::read_whole_region()
{
  for (int n = 1; n != 32; ++n) {

    prologue_return ret = prologue(n);
    const void* read    = MC_region_read(&(ret.region), ret.dstn, ret.src, ret.size);
    INFO("Mismatch in MC_region_read()");
    REQUIRE(not memcmp(ret.src, read, ret.size));

    munmap(ret.dstn, ret.size);
    munmap(ret.src, ret.size);
  }
}

void snap_test_helper::read_region_parts()
{
  for (int n = 1; n != 32; ++n) {

    prologue_return ret = prologue(n);

    for (int j = 0; j != 100; ++j) {
      size_t offset    = rnd_engine() % ret.size;
      size_t size      = rnd_engine() % (ret.size - offset);
      const void* read = MC_region_read(&(ret.region), ret.dstn, (const char*)ret.src + offset, size);
      INFO("Mismatch in MC_region_read()");
      REQUIRE(not memcmp((char*)ret.src + offset, read, size));
    }
    munmap(ret.dstn, ret.size);
    munmap(ret.src, ret.size);
  }
}

void snap_test_helper::compare_whole_region()
{
  for (int n = 1; n != 32; ++n) {

    prologue_return ret = prologue(n);

    INFO("Unexpected match in MC_snapshot_region_memcmp() with previous snapshot");
    REQUIRE(MC_snapshot_region_memcmp(ret.src, &(ret.region0), ret.src, &(ret.region), ret.size));

    munmap(ret.dstn, ret.size);
    munmap(ret.src, ret.size);
  }
}

void snap_test_helper::compare_region_parts()
{
  for (int n = 1; n != 32; ++n) {

    prologue_return ret = prologue(n);

    for (int j = 0; j != 100; ++j) {
      size_t offset = rnd_engine() % ret.size;
      size_t size   = rnd_engine() % (ret.size - offset);

      INFO("Mismatch in MC_snapshot_region_memcmp()");
      REQUIRE(not MC_snapshot_region_memcmp((char*)ret.src + offset, &(ret.region), (char*)ret.src + offset,
                                            &(ret.region), size));
    }
    munmap(ret.dstn, ret.size);
    munmap(ret.src, ret.size);
  }
}

void snap_test_helper::read_pointer()
{

  prologue_return ret = prologue(1);
  memcpy(ret.src, &mc_model_checker, sizeof(void*));
  simgrid::mc::RegionSnapshot region2 =
      simgrid::mc::sparse_region(simgrid::mc::RegionType::Unknown, ret.src, ret.src, ret.size);
  INFO("Mismtach in MC_region_read_pointer()");
  REQUIRE(MC_region_read_pointer(&region2, ret.src) == mc_model_checker);

  munmap(ret.dstn, ret.size);
  munmap(ret.src, ret.size);
}

/*************** End: class snap_test_helper *****************************/

TEST_CASE("MC::Snapshot: A copy/snapshot of a given memory region", "MC::Snapshot")
{
  auto sparse = GENERATE(false, true);

  if (sparse) {
    INFO("Sparse snapshot (using pages)");
  } else {
    INFO("Flat snapshot (no pages)");
  }

  snap_test_helper::Init(sparse);

  INFO("Read whole region");
  snap_test_helper::read_whole_region();

  INFO("Read region parts");
  snap_test_helper::read_region_parts();

  INFO("Compare whole region");
  snap_test_helper::compare_whole_region();

  INFO("Compare region parts");
  snap_test_helper::compare_region_parts();

  INFO("Read pointer");
  snap_test_helper::read_pointer();

  snap_test_helper::cleanup();
}
