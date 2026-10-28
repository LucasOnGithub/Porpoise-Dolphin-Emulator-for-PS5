// Only linked into the experimental host test build.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <execinfo.h>
#include <thread>
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/Logging/LogManager.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigLoaders/BaseConfigLoader.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "Core/HW/Memmap.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/Slippi/SlippiNetplay.h"
#include "Core/Slippi/SlippiSavestate.h"
#include "Core/Slippi/SlippiStringUtil.h"
#include "Core/System.h"
#include "DolphinLibretro/Common/Globals.h"
#include "UICommon/UICommon.h"
extern "C" size_t retro_serialize_size();
extern "C" bool retro_serialize(void*, size_t);
extern "C" bool retro_unserialize(const void*, size_t);
namespace
{
unsigned failures, checks;
bool CoreTestEnvironment(unsigned, void*) { return false; }
void Check(bool ok, const char* message)
{
  ++checks;
  if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
void CoreTestCrashSignal(int signal)
{
  void* frames[40];
  backtrace_symbols_fd(frames, backtrace(frames, 40), 2);
  std::_Exit(128 + signal);
}
template<class Predicate> bool WaitFor(Predicate predicate)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
  do { if (predicate()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
  while (std::chrono::steady_clock::now() < deadline);
  return false;
}
u16 FreePort()
{
  ENetAddress address{ENET_HOST_ANY, 0};
  auto* host = enet_host_create(&address, 1, 1, 0, 0);
  if (!host) return 0;
  enet_socket_get_address(host->socket, &address);
  enet_host_destroy(host);
  return address.port;
}
}
extern "C" __attribute__((visibility("default"))) int porpoise_slippi_core_tests(const char* directory)
{
  std::signal(SIGSEGV, CoreTestCrashSignal);
  std::signal(SIGABRT, CoreTestCrashSignal);
  Libretro::environ_cb = CoreTestEnvironment;
  failures = checks = 0;
  std::fprintf(stderr, "Core test: initialize synthetic GameCube memory\n");
  Core::DeclareAsCPUThread();
  UICommon::SetUserDirectory(directory);
  Config::Init();
  Config::AddLayer(ConfigLoaders::GenerateBaseConfigLoader());
  SConfig::Init();
  Common::Log::LogManager::Init();
  auto& system = Core::System::GetInstance();
  Config::SetCurrent(Config::MAIN_CPU_THREAD, false);
  Config::SetCurrent(Config::MAIN_MMU, false);
  Config::SetCurrent(Config::SLIPPI_ENABLE_SPECTATOR, false);
  Config::SetCurrent(Config::SLIPPI_ENABLE_JUKEBOX, false);
  system.Initialize();
  system.GetCoreTiming().Init();
  auto& memory = system.GetMemory();
  memory.Init();
  system.GetPowerPC().Init(PowerPC::CPUCore::Interpreter);
  // A booted GameCube has a cached MEM1 BAT mapping. No game executes in this
  // fixture, so install the translated BAT entries explicitly for HostRead.
  system.GetPPCState().msr.DR = 1;
  auto& bat = system.GetMMU().GetDBATTable();
  for (u32 off = 0; off < memory.GetRamSizeReal(); off += PowerPC::BAT_PAGE_SIZE)
    bat[(0x80000000u + off) >> PowerPC::BAT_INDEX_SHIFT] = off | PowerPC::BAT_MAPPED_BIT | PowerPC::BAT_PHYSICAL_BIT;
  Check(memory.GetRamSizeReal() == 0x1800000, "GameCube MEM1 allocation");
  {
    SlippiSavestate::should_force_init = true;
    SlippiSavestate invalid;
    Check(!invalid.IsValid(), "invalid heap rejected");
    memory.Write_U32(0x80bd5c40, 0x804d76b8);
    memory.Write_U32(0x80bd6c40, 0x804d76bc);
    SlippiSavestate state;
    Check(state.IsValid(), "valid heap accepted");
    memory.Write_U32(0x11223344, 0x803b7300);
    memory.Write_U32(0x11111111, 0x803b7310);
    memory.Write_U32(0x22222222, 0x804031a0);
    memory.Write_U32(0x33333333, 0x80bd5c40);
    state.Capture();
    memory.Write_U32(0xaabbccdd, 0x803b7300);
    memory.Write_U32(0xaaaaaaaa, 0x803b7310);
    memory.Write_U32(0xbbbbbbbb, 0x804031a0);
    memory.Write_U32(0xcccccccc, 0x80bd5c40);
    state.Load({{0x803b7310,4}});
    Check(memory.Read_U32(0x803b7300) == 0x11223344, "rollback restores game data");
    Check(memory.Read_U32(0x803b7310) == 0xaaaaaaaa, "rollback preserves requested input");
    Check(memory.Read_U32(0x804031a0) == 0xbbbbbbbb, "rollback excludes audio state");
    Check(memory.Read_U32(0x80bd5c40) == 0x33333333, "rollback restores dynamic heap");
    memory.Write_U32(0x12345678, 0x803b7300);
    state.Load({{0x817fffff,0xffffffff}});
    Check(memory.Read_U32(0x803b7300) == 0x12345678, "invalid preserve range rejected");
  }
  std::fprintf(stderr, "Core test: text conversion\n");
  Check(TruncateLengthChar("A\xf0\x9f\x90\xac" "B",2) == "A\xf0\x9f\x90\xac", "UTF-8 character truncation");
  const auto name = ConvertStringForGame("A#",2);
  Check(name.size()==5 && static_cast<u8>(name[1])==0x81 && static_cast<u8>(name[2])==0x94, "Shift-JIS punctuation bytes");
  std::fprintf(stderr, "Core test: EXI factory, Rust ownership and DMA\n");
  File::CreateFullPath(File::GetUserPath(D_SLIPPI_IDX));
  Config::SetCurrent(Config::SLIPPI_ISO_PATH,std::string("missing-synthetic-test.iso"));
  for (unsigned cycle=0;cycle<2;++cycle)
  {
    auto exi=ExpansionInterface::EXIDevice_Create(system,ExpansionInterface::EXIDeviceType::Slippi,1,{});
    Check(exi && exi->IsPresent(), "Slot B Slippi factory");
    memory.Write_U8(0xd5,0x80006000);
    exi->DMAWrite(0x80006000,1);exi->DMARead(0x80006100,2);
    Check(memory.Read_U8(0x80006100)==1 && memory.Read_U8(0x80006101)==2,"EXI online delay response");
    memory.Write_U8(0x35,0x80006000);exi->DMAWrite(0x80006000,1);exi->DMAWrite(0x80006000,0);
    memory.Write_U8(0xb0,0x80006000);exi->DMAWrite(0x80006000,1);exi->DMARead(0x80006100,1);
  }
  Config::SetCurrent(Config::SLIPPI_ENABLED,true);
  std::array<u8,8> buffer{};
  Check(retro_serialize_size()==0 && !retro_serialize(buffer.data(),buffer.size()) && !retro_unserialize(buffer.data(),buffer.size()),"libretro states blocked");
  Config::SetCurrent(Config::SLIPPI_ENABLED,false);
  std::fprintf(stderr,"Core test: Slippi peers on loopback UDP\n");
  Check(enet_initialize()==0,"ENet initialization");
  const auto pa=FreePort();auto pb=FreePort();if(pb==pa)pb=FreePort();
  Check(pa && pb && pa!=pb,"distinct local ports");
  if(pa && pb && pa!=pb)
  {
    auto a=std::make_unique<SlippiNetplayClient>(std::vector<std::string>{"127.0.0.1"},std::vector<u16>{pb},1,pa,true,0);
    auto b=std::make_unique<SlippiNetplayClient>(std::vector<std::string>{"127.0.0.1"},std::vector<u16>{pa},1,pb,false,1);
    using Status=SlippiNetplayClient::SlippiConnectStatus;
    const bool connected=WaitFor([&]{return a->GetSlippiConnectStatus()==Status::NET_CONNECT_STATUS_CONNECTED && b->GetSlippiConnectStatus()==Status::NET_CONNECT_STATUS_CONNECTED;});
    Check(connected,"Slippi peer connection");
    if(connected)
    {
      a->StartSlippiGame();b->StartSlippiGame();
      u8 input[SLIPPI_PAD_DATA_SIZE]={0x12,0x34,0x56,0x78};
      a->SendSlippiPad(std::make_unique<SlippiPad>(1,input));
      Check(WaitFor([&]{auto r=b->GetSlippiRemotePad(0,7);return r->latest_frame==1 && r->data.size()>=sizeof(input) && std::equal(input,input+sizeof(input),r->data.begin());}),"exact peer input delivery");
    }
    b.reset();a.reset();Check(!IsOnline(),"peer teardown clears status");
  }
  enet_deinitialize();
  std::fprintf(stderr,"Core test: shutdown\n");
  system.GetPowerPC().Shutdown();memory.Shutdown();system.GetCoreTiming().Shutdown();
  // Logger shutdown saves settings through Config, so it must precede Config destruction.
  Common::Log::LogManager::Shutdown();
  SConfig::Shutdown();Config::Shutdown();Core::UndeclareAsCPUThread();
  std::fprintf(stderr,"Slippi core: %u checks, %u failures\n",checks,failures);
  return failures?1:0;
}
