#!/usr/bin/env python3
"""把当前固件导出成一份能直接用 Keil 打开的工程。

这份脚本只拷文件、写工程，不改应用源文件，也不改 lib/rfid_core。
默认把工程写到仓库外的目录。加上 --inplace 才会改当前工作树。
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

APP_C_FILES = [
    "src/main.c",
    "src/app.c",
    "src/announce.c",
    "src/soft_start.c",
    "src/board_stm32.c",
]

RFID_C_FILES = [
    "lib/rfid_core/src/rfid_protocol.c",
    "lib/rfid_core/src/rfid_reader.c",
]

HAL_C_FILES = [
    "stm32f1xx_hal.c",
    "stm32f1xx_hal_adc.c",
    "stm32f1xx_hal_adc_ex.c",
    "stm32f1xx_hal_cortex.c",
    "stm32f1xx_hal_dma.c",
    "stm32f1xx_hal_exti.c",
    "stm32f1xx_hal_flash.c",
    "stm32f1xx_hal_flash_ex.c",
    "stm32f1xx_hal_gpio.c",
    "stm32f1xx_hal_gpio_ex.c",
    "stm32f1xx_hal_pwr.c",
    "stm32f1xx_hal_rcc.c",
    "stm32f1xx_hal_rcc_ex.c",
    "stm32f1xx_hal_tim.c",
    "stm32f1xx_hal_tim_ex.c",
    "stm32f1xx_hal_uart.c",
]

INCLUDE_PATH = (
    "../include;"
    "../lib/rfid_core/include;"
    "../Core/Inc;"
    "../Drivers/STM32F1xx_HAL_Driver/Inc;"
    "../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy;"
    "../Drivers/CMSIS/Device/ST/STM32F1xx/Include;"
    "../Drivers/CMSIS/Include"
)

DEFINES = "USE_HAL_DRIVER,STM32F103x6,APP_ENABLE_MOTOR=1"

GITIGNORE = """\
.DS_Store
.cache/
.pio/
*.plist
MDK-ARM/2027-control/
MDK-ARM/*.uvguix.*
MDK-ARM/*.bak
MDK-ARM/*.dep
*.o
*.d
*.crf
*.axf
*.hex
*.bin
*.map
*.htm
*.lnp
*.lst
JLinkLog.txt
EventRecorderStub.scvd
compile_commands.json
工创赛控制/.obsidian/workspace.json
scripts/__pycache__/
"""


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_hal_src() -> Path:
    return repo_root().parent / "2027省赛STM32代码"


def copy_tree(src: Path, dst: Path) -> None:
    if not src.is_dir():
        raise FileNotFoundError("找不到目录：%s" % src)
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def xml_file(name: str, path: str, file_type: int) -> str:
    return (
        "            <File>\n"
        "              <FileName>%s</FileName>\n"
        "              <FileType>%d</FileType>\n"
        "              <FilePath>%s</FilePath>\n"
        "            </File>\n" % (name, file_type, path)
    )


def xml_group(name: str, files: list[tuple[str, str, int]]) -> str:
    body = "".join(xml_file(item[0], item[1], item[2]) for item in files)
    return (
        "        <Group>\n"
        "          <GroupName>%s</GroupName>\n"
        "          <Files>\n"
        "%s"
        "          </Files>\n"
        "        </Group>\n" % (name, body)
    )


def build_groups() -> str:
    startup = [
        ("startup_stm32f103x6.s", "startup_stm32f103x6.s", 2),
    ]
    app = [(Path(p).name, "../" + p.replace("\\", "/"), 1) for p in APP_C_FILES]
    rfid = [(Path(p).name, "../" + p.replace("\\", "/"), 1) for p in RFID_C_FILES]
    hal = [
        (
            name,
            "../Drivers/STM32F1xx_HAL_Driver/Src/" + name,
            1,
        )
        for name in HAL_C_FILES
    ]
    cmsis = [("system_stm32f1xx.c", "../Core/Src/system_stm32f1xx.c", 1)]
    return (
        xml_group("Application/MDK-ARM", startup)
        + xml_group("Application", app)
        + xml_group("rfid_core", rfid)
        + xml_group("Drivers/STM32F1xx_HAL_Driver", hal)
        + xml_group("Drivers/CMSIS", cmsis)
    )


def uvprojx_text() -> str:
    return """\
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<Project xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_projx.xsd">

  <SchemaVersion>2.1</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>

  <Targets>
    <Target>
      <TargetName>2027-control</TargetName>
      <ToolsetNumber>0x4</ToolsetNumber>
      <ToolsetName>ARM-ADS</ToolsetName>
      <pCCUsed>5060960::V5.06 update 7 (build 960)::.\\ARMCC</pCCUsed>
      <uAC6>0</uAC6>
      <TargetOption>
        <TargetCommonOption>
          <Device>STM32F103C6</Device>
          <Vendor>STMicroelectronics</Vendor>
          <PackID>Keil.STM32F1xx_DFP.2.2.0</PackID>
          <PackURL>http://www.keil.com/pack/</PackURL>
          <Cpu>IRAM(0x20000000,0x2800) IROM(0x08000000,0x8000) CPUTYPE("Cortex-M3") CLOCK(8000000) ELITTLE</Cpu>
          <FlashUtilSpec></FlashUtilSpec>
          <StartupFile></StartupFile>
          <FlashDriverDll>UL2CM3(-S0 -C0 -P0 -FD20000000 -FC1000 -FN1 -FF0STM32F10x_32 -FS08000000 -FL08000 -FP0($$Device:STM32F103C6$Flash\\STM32F10x_32.FLM))</FlashDriverDll>
          <DeviceId>0</DeviceId>
          <RegisterFile>$$Device:STM32F103C6$Device\\Include\\stm32f10x.h</RegisterFile>
          <SFDFile>$$Device:STM32F103C6$SVD\\STM32F103xx.svd</SFDFile>
          <bCustSvd>0</bCustSvd>
          <UseEnv>0</UseEnv>
          <BinPath></BinPath>
          <IncludePath></IncludePath>
          <LibPath></LibPath>
          <RegisterFilePath></RegisterFilePath>
          <DBRegisterFilePath></DBRegisterFilePath>
          <TargetStatus>
            <Error>0</Error>
            <ExitCodeStop>0</ExitCodeStop>
            <ButtonStop>0</ButtonStop>
            <NotGenerated>0</NotGenerated>
            <InvalidFlash>0</InvalidFlash>
          </TargetStatus>
          <OutputDirectory>2027-control\\</OutputDirectory>
          <OutputName>2027-control</OutputName>
          <CreateExecutable>1</CreateExecutable>
          <CreateLib>0</CreateLib>
          <CreateHexFile>1</CreateHexFile>
          <DebugInformation>1</DebugInformation>
          <BrowseInformation>1</BrowseInformation>
          <ListingPath></ListingPath>
          <HexFormatSelection>1</HexFormatSelection>
          <Merge32K>0</Merge32K>
          <CreateBatchFile>0</CreateBatchFile>
          <BeforeCompile>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopU1X>0</nStopU1X>
            <nStopU2X>0</nStopU2X>
          </BeforeCompile>
          <BeforeMake>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopB1X>0</nStopB1X>
            <nStopB2X>0</nStopB2X>
          </BeforeMake>
          <AfterMake>
            <RunUserProg1>0</RunUserProg1>
            <RunUserProg2>0</RunUserProg2>
            <UserProg1Name></UserProg1Name>
            <UserProg2Name></UserProg2Name>
            <UserProg1Dos16Mode>0</UserProg1Dos16Mode>
            <UserProg2Dos16Mode>0</UserProg2Dos16Mode>
            <nStopA1X>0</nStopA1X>
            <nStopA2X>0</nStopA2X>
          </AfterMake>
          <SelectedForBatchBuild>1</SelectedForBatchBuild>
          <SVCSIdString></SVCSIdString>
        </TargetCommonOption>
        <CommonProperty>
          <UseCPPCompiler>0</UseCPPCompiler>
          <RVCTCodeConst>0</RVCTCodeConst>
          <RVCTZI>0</RVCTZI>
          <RVCTOtherData>0</RVCTOtherData>
          <ModuleSelection>0</ModuleSelection>
          <IncludeInBuild>1</IncludeInBuild>
          <AlwaysBuild>0</AlwaysBuild>
          <GenerateAssemblyFile>0</GenerateAssemblyFile>
          <AssembleAssemblyFile>0</AssembleAssemblyFile>
          <PublicsOnly>0</PublicsOnly>
          <StopOnExitCode>3</StopOnExitCode>
          <CustomArgument></CustomArgument>
          <IncludeLibraryModules></IncludeLibraryModules>
          <ComprImg>0</ComprImg>
        </CommonProperty>
        <DllOption>
          <SimDllName>SARMCM3.DLL</SimDllName>
          <SimDllArguments> -REMAP</SimDllArguments>
          <SimDlgDll>DCM.DLL</SimDlgDll>
          <SimDlgDllArguments>-pCM3</SimDlgDllArguments>
          <TargetDllName>SARMCM3.DLL</TargetDllName>
          <TargetDllArguments></TargetDllArguments>
          <TargetDlgDll>TCM.DLL</TargetDlgDll>
          <TargetDlgDllArguments>-pCM3</TargetDlgDllArguments>
        </DllOption>
        <DebugOption>
          <OPTHX>
            <HexSelection>1</HexSelection>
            <HexRangeLowAddress>0</HexRangeLowAddress>
            <HexRangeHighAddress>0</HexRangeHighAddress>
            <HexOffset>0</HexOffset>
            <Oh166RecLen>16</Oh166RecLen>
          </OPTHX>
        </DebugOption>
        <Utilities>
          <Flash1>
            <UseTargetDll>1</UseTargetDll>
            <UseExternalTool>0</UseExternalTool>
            <RunIndependent>0</RunIndependent>
            <UpdateFlashBeforeDebugging>1</UpdateFlashBeforeDebugging>
            <Capability>1</Capability>
            <DriverSelection>4096</DriverSelection>
          </Flash1>
          <bUseTDR>1</bUseTDR>
          <Flash2>BIN\\UL2CM3.DLL</Flash2>
          <Flash3>"" ()</Flash3>
          <Flash4></Flash4>
          <pFcarmOut></pFcarmOut>
          <pFcarmGrp></pFcarmGrp>
          <pFcArmRoot></pFcArmRoot>
          <FcArmLst>0</FcArmLst>
        </Utilities>
        <TargetArmAds>
          <ArmAdsMisc>
            <GenerateListings>0</GenerateListings>
            <asHll>1</asHll>
            <asAsm>1</asAsm>
            <asMacX>1</asMacX>
            <asSyms>1</asSyms>
            <asFals>1</asFals>
            <asDbgD>1</asDbgD>
            <asForm>1</asForm>
            <ldLst>0</ldLst>
            <ldmm>1</ldmm>
            <ldXref>1</ldXref>
            <BigEnd>0</BigEnd>
            <AdsALst>1</AdsALst>
            <AdsACrf>1</AdsACrf>
            <AdsANop>0</AdsANop>
            <AdsANot>0</AdsANot>
            <AdsLLst>1</AdsLLst>
            <AdsLmap>1</AdsLmap>
            <AdsLcgr>1</AdsLcgr>
            <AdsLsym>1</AdsLsym>
            <AdsLszi>1</AdsLszi>
            <AdsLtoi>1</AdsLtoi>
            <AdsLsun>1</AdsLsun>
            <AdsLven>1</AdsLven>
            <AdsLsxf>1</AdsLsxf>
            <RvctClst>0</RvctClst>
            <GenPPlst>0</GenPPlst>
            <AdsCpuType>"Cortex-M3"</AdsCpuType>
            <RvctDeviceName></RvctDeviceName>
            <mOS>0</mOS>
            <uocRom>0</uocRom>
            <uocRam>0</uocRam>
            <hadIROM>1</hadIROM>
            <hadIRAM>1</hadIRAM>
            <hadXRAM>0</hadXRAM>
            <uocXRam>0</uocXRam>
            <RvdsVP>0</RvdsVP>
            <hadIRAM2>0</hadIRAM2>
            <hadIROM2>0</hadIROM2>
            <StupSel>8</StupSel>
            <useUlib>1</useUlib>
            <EndSel>0</EndSel>
            <uLtcg>0</uLtcg>
            <nSecure>0</nSecure>
            <RoSelD>3</RoSelD>
            <RwSelD>3</RwSelD>
            <CodeSel>0</CodeSel>
            <OnChipMemories>
              <IRAM>
                <Type>0</Type>
                <StartAddress>0x20000000</StartAddress>
                <Size>0x2800</Size>
              </IRAM>
              <IROM>
                <Type>1</Type>
                <StartAddress>0x8000000</StartAddress>
                <Size>0x8000</Size>
              </IROM>
              <OCR_RVCT4>
                <Type>1</Type>
                <StartAddress>0x8000000</StartAddress>
                <Size>0x8000</Size>
              </OCR_RVCT4>
              <OCR_RVCT9>
                <Type>0</Type>
                <StartAddress>0x20000000</StartAddress>
                <Size>0x2800</Size>
              </OCR_RVCT9>
            </OnChipMemories>
            <RvctStartVector></RvctStartVector>
          </ArmAdsMisc>
          <Cads>
            <interw>1</interw>
            <Optim>4</Optim>
            <oTime>0</oTime>
            <SplitLS>0</SplitLS>
            <OneElfS>1</OneElfS>
            <Strict>0</Strict>
            <EnumInt>0</EnumInt>
            <PlainCh>0</PlainCh>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <wLevel>3</wLevel>
            <uThumb>0</uThumb>
            <uSurpInc>0</uSurpInc>
            <uC99>1</uC99>
            <uGnu>0</uGnu>
            <useXO>0</useXO>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define>%s</Define>
              <Undefine></Undefine>
              <IncludePath>%s</IncludePath>
            </VariousControls>
          </Cads>
          <Aads>
            <interw>1</interw>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <thumb>0</thumb>
            <SplitLS>0</SplitLS>
            <SwStkChk>0</SwStkChk>
            <NoWarn>0</NoWarn>
            <uSurpInc>0</uSurpInc>
            <useXO>0</useXO>
            <VariousControls>
              <MiscControls></MiscControls>
              <Define></Define>
              <Undefine></Undefine>
              <IncludePath>../Core/Inc</IncludePath>
            </VariousControls>
          </Aads>
          <LDads>
            <umfTarg>1</umfTarg>
            <Ropi>0</Ropi>
            <Rwpi>0</Rwpi>
            <noStLib>0</noStLib>
            <RepFail>1</RepFail>
            <useFile>0</useFile>
            <TextAddressRange></TextAddressRange>
            <DataAddressRange></DataAddressRange>
            <ScatterFile></ScatterFile>
            <IncludeLibs></IncludeLibs>
            <IncludeLibsPath></IncludeLibsPath>
            <Misc></Misc>
            <LinkerInputFile></LinkerInputFile>
            <DisabledWarnings></DisabledWarnings>
          </LDads>
        </TargetArmAds>
      </TargetOption>
      <Groups>
%s      </Groups>
    </Target>
  </Targets>

  <RTE>
    <apis/>
    <components>
      <component Cclass="CMSIS" Cgroup="CORE" Cvendor="ARM" Cversion="5.5.0" condition="ARMv6_7_8-M Device">
        <package name="CMSIS" schemaVersion="1.3" url="http://www.keil.com/pack/" vendor="ARM" version="5.8.0"/>
        <targetInfos>
          <targetInfo name="2027-control"/>
        </targetInfos>
      </component>
    </components>
    <files/>
  </RTE>

</Project>
""" % (
        DEFINES,
        INCLUDE_PATH,
        build_groups(),
    )


def uvoptx_text() -> str:
    flash = (
        "UL2CM3(-S0 -C0 -P0 ) -FN1 -FC1000 -FD20000000 "
        "-FF0STM32F10x_32 -FL08000 -FS08000000 "
        "-FP0($$Device:STM32F103C6$Flash\\STM32F10x_32.FLM)"
    )
    return """\
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<ProjectOpt xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="project_optx.xsd">

  <SchemaVersion>1.0</SchemaVersion>
  <Header>### uVision Project, (C) Keil Software</Header>

  <Extensions>
    <cExt>*.c</cExt>
    <aExt>*.s*; *.src; *.a*</aExt>
    <oExt>*.obj; *.o</oExt>
    <lExt>*.lib</lExt>
    <tExt>*.txt; *.h; *.inc; *.md</tExt>
    <pExt>*.plm</pExt>
    <CppX>*.cpp</CppX>
    <nMigrate>0</nMigrate>
  </Extensions>

  <DaveTm>
    <dwLowDateTime>0</dwLowDateTime>
    <dwHighDateTime>0</dwHighDateTime>
  </DaveTm>

  <Target>
    <TargetName>2027-control</TargetName>
    <ToolsetNumber>0x4</ToolsetNumber>
    <ToolsetName>ARM-ADS</ToolsetName>
    <TargetOption>
      <CLKADS>8000000</CLKADS>
      <OPTTT>
        <gFlags>1</gFlags>
        <BeepAtEnd>1</BeepAtEnd>
        <RunSim>0</RunSim>
        <RunTarget>1</RunTarget>
        <RunAbUc>0</RunAbUc>
      </OPTTT>
      <OPTHX>
        <HexSelection>1</HexSelection>
        <FlashByte>65535</FlashByte>
        <HexRangeLowAddress>0</HexRangeLowAddress>
        <HexRangeHighAddress>0</HexRangeHighAddress>
        <HexOffset>0</HexOffset>
      </OPTHX>
      <OPTFL>
        <tvExp>1</tvExp>
        <tvExpOptDlg>0</tvExpOptDlg>
        <IsCurrentTarget>1</IsCurrentTarget>
      </OPTFL>
      <CpuCode>18</CpuCode>
      <DebugOpt>
        <uSim>0</uSim>
        <uTrg>1</uTrg>
        <sLdApp>1</sLdApp>
        <sGomain>1</sGomain>
        <sRbreak>1</sRbreak>
        <sRwatch>1</sRwatch>
        <sRmem>1</sRmem>
        <sRfunc>1</sRfunc>
        <sRbox>1</sRbox>
        <tLdApp>1</tLdApp>
        <tGomain>1</tGomain>
        <tRbreak>1</tRbreak>
        <tRwatch>1</tRwatch>
        <tRmem>1</tRmem>
        <tRfunc>1</tRfunc>
        <tRbox>1</tRbox>
        <tRtrace>1</tRtrace>
        <nTsel>6</nTsel>
        <pMon>STLink\\ST-LINKIII-KEIL_SWO.dll</pMon>
      </DebugOpt>
      <TargetDriverDllRegistry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>UL2CM3</Key>
          <Name>%s</Name>
        </SetRegEntry>
        <SetRegEntry>
          <Number>0</Number>
          <Key>ST-LINKIII-KEIL_SWO</Key>
          <Name>-U -O206 -SF4000 -C0 -A0 -I0 -HNlocalhost -HP7184 -P00 -N00("ARM CoreSight SW-DP") -D00(1BA01477) -L00(0) -TO18 -TC10000000 -TP21 -TDS8007 -TDT0 -TDC1F -TIEFFFFFFFF -TIP8 -FO7 -FD20000000 -FC1000 -FN1 -FF0STM32F10x_32.FLM -FS08000000 -FL08000 -FP0($$Device:STM32F103C6$Flash\\STM32F10x_32.FLM)</Name>
        </SetRegEntry>
      </TargetDriverDllRegistry>
      <Breakpoint/>
      <Tracepoint>
        <THDelay>0</THDelay>
      </Tracepoint>
      <DebugFlag>
        <trace>0</trace>
        <periodic>1</periodic>
        <aLwin>1</aLwin>
        <aCover>0</aCover>
        <aSer1>0</aSer1>
        <aSer2>0</aSer2>
        <aPa>0</aPa>
        <viewmode>1</viewmode>
        <vrSel>0</vrSel>
        <aSym>0</aSym>
        <aTbox>0</aTbox>
        <AscS1>0</AscS1>
        <AscS2>0</AscS2>
        <AscS3>0</AscS3>
        <aSer3>0</aSer3>
        <eProf>0</eProf>
        <aLa>0</aLa>
        <aPa1>0</aPa1>
        <AscS4>0</AscS4>
        <aSer4>0</aSer4>
        <StkLoc>1</StkLoc>
        <TrcWin>0</TrcWin>
        <newCpu>0</newCpu>
        <uProt>0</uProt>
      </DebugFlag>
      <LintExecutable></LintExecutable>
      <LintConfigFile></LintConfigFile>
      <bLintAuto>0</bLintAuto>
      <bAutoGenD>0</bAutoGenD>
      <LntExFlags>0</LntExFlags>
      <pMisraName></pMisraName>
      <pszMrule></pszMrule>
      <pSingCmds></pSingCmds>
      <pMultCmds></pMultCmds>
      <pMisraNamep></pMisraNamep>
      <pszMrulep></pszMrulep>
      <pSingCmdsp></pSingCmdsp>
      <pMultCmdsp></pMultCmdsp>
      <DebugDescription>
        <Enable>1</Enable>
        <EnableLog>0</EnableLog>
        <Protocol>2</Protocol>
        <DbgClock>10000000</DbgClock>
      </DebugDescription>
    </TargetOption>
  </Target>

</ProjectOpt>
""" % flash


def keil_readme_text() -> str:
    return """\
# 2027 工创赛 RFID 电动车控制固件（Keil 工程）

仓库：<https://github.com/stm32-hit-team/2027-control-firmware>

**这是 `keil` 分支。** 用 Keil 5（MDK-ARM）打开下面这个文件即可：

```
MDK-ARM/2027-control.uvprojx
```

这里没有 `platformio.ini`，也不依赖 PlatformIO。日常开发仍在 [`main`](https://github.com/stm32-hit-team/2027-control-firmware/tree/main) 分支。

更细的说明在 [`工创赛控制/`](工创赛控制/index.md)。转到 Keil 的专页是 [工创赛控制/08-Keil工程.md](工创赛控制/08-Keil工程.md)。

---

## 1. 打开、编译、烧录

1. 安装 Keil 5，并用 Pack Installer 装好 `Keil.STM32F1xx_DFP`。
2. 克隆本仓库后切到这个分支：

```sh
git clone https://github.com/stm32-hit-team/2027-control-firmware.git
cd 2027-control-firmware
git checkout keil
```

3. 双击 `MDK-ARM/2027-control.uvprojx`。
4. 菜单 Rebuild。
5. 用 ST-Link 接 `SWD`（`PA13`、`PA14`、`NRST`、地、3.3 V），再 Download。

芯片是 **STM32F103C6**。Flash 32 KB，RAM 10 KB。不要改成 C8。

烧完芯片会复位。约 3 秒后电机可能转，先架空车轮。

---

## 2. 说明文档

| 页 | 讲什么 |
|---|---|
| [文档首页](工创赛控制/index.md) | 从哪看起 |
| [总览](工创赛控制/00-总览.md) | 产品做什么、分几层 |
| [硬件对照](工创赛控制/01-硬件对照.md) | 插座、开关、单片机脚 |
| [主循环](工创赛控制/02-主循环.md) | 上电后程序怎么转 |
| [读卡到播报](工创赛控制/03-读卡到播报.md) | 一张卡从读到念 |
| [模块原理](工创赛控制/07-模块原理.md) | RFID、语音、缓启动、电机、灯 |
| [转到 Keil](工创赛控制/08-Keil工程.md) | 本分支怎么用，以及和 `main` 怎么对应 |
| [故障排查](工创赛控制/06-故障排查.md) | 灯、卡、语音、电机常见问题 |

---

## 3. 代码里有哪些部分

应用层在 `src/`、`include/`。读卡协议和状态机在 `lib/rfid_core/`，不要改。HAL 和 CMSIS 在 `Drivers/`，不要改。

```
main.c          上电入口
app.c           读到卡就立刻发文字，并点灯
announce.c      立刻播报。没有队列
soft_start.c    把旋钮 ADC 换成停车毫秒数
board_stm32.c   时钟、脚、串口、旋钮、电机、灯
rfid_protocol   串口帧的拼和拆（库）
rfid_reader     寻卡、读块、去重、确认拿走（库）
```

要点：

- RFID 走 USART1。上电先 9600 改速，再 115200 工作。
- 语音走 USART2，9600。读到就念，下一张不同 UID 的卡会打断上一句。
- 缓启动：上电先等 3 秒，再约 3 秒爬到巡航。停车时间由 `PB1` 旋钮决定。
- 旋钮只管「跑多久后停」，不管「等多久才起步」。

原理见 [模块原理](工创赛控制/07-模块原理.md)。

---

## 4. 工程已经配好的项

| 项 | 值 |
|---|---|
| 芯片 | STM32F103C6 |
| IROM1 | `0x08000000`，`0x8000` |
| IRAM1 | `0x20000000`，`0x2800` |
| 宏 | `USE_HAL_DRIVER`、`STM32F103x6`、`APP_ENABLE_MOTOR=1` |
| C 语言 | C99 |
| 启动文件 | `startup_stm32f103x6.s` |
| 调试器 | ST-Link，SWD |

`tts_service.c` 没有加入工程。应用层用立刻播报。

改参数改 `include/app_config.h`，然后 Rebuild。

---

## 5. 和 main 分支怎么同步

`main` 改了 `src/`、`include/`、`lib/rfid_core/` 之后：

```sh
git checkout keil
git checkout main -- src include lib/rfid_core 工创赛控制
```

然后 Rebuild。`Drivers/`、`Core/`、启动文件不用从 `main` 覆盖。

如果 `src/` 新增了 `.c` 文件，还要在 Keil 里把新文件加进工程。

---

## 6. 常见问题

**打开工程提示缺 Pack**  
用 Pack Installer 安装 `Keil.STM32F1xx_DFP`。

**`SysTick_Handler` 重复定义**  
不要再加入 Cube 生成的 `stm32f1xx_it.c`。

**能编译，板子没反应**  
确认设备是 C6，Flash 是 32 KB，`HSE_VALUE` 是 8 MHz。

**灯、卡、语音、电机**  
见 [故障排查](工创赛控制/06-故障排查.md)。

不要打开仓库旁边那份 2027 省赛 Keil 工程来编这份固件。
"""


def copy_vendor(hal_src: Path, dest: Path) -> None:
    drivers_src = hal_src / "Drivers"
    if not drivers_src.is_dir():
        raise FileNotFoundError("找不到 HAL 目录：%s" % drivers_src)

    copy_tree(
        drivers_src / "STM32F1xx_HAL_Driver",
        dest / "Drivers" / "STM32F1xx_HAL_Driver",
    )
    copy_tree(drivers_src / "CMSIS", dest / "Drivers" / "CMSIS")

    shutil.copy2(
        hal_src / "Core" / "Inc" / "stm32f1xx_hal_conf.h",
        dest / "Core" / "Inc" / "stm32f1xx_hal_conf.h",
    )
    shutil.copy2(
        hal_src / "Core" / "Src" / "system_stm32f1xx.c",
        dest / "Core" / "Src" / "system_stm32f1xx.c",
    )

    startup = hal_src / "MDK-ARM" / "startup_stm32f103x6.s"
    if not startup.is_file():
        startup = hal_src / "startup_stm32f103x6.s"
    shutil.copy2(startup, dest / "MDK-ARM" / "startup_stm32f103x6.s")


def copy_app_tree(src_root: Path, dest: Path) -> None:
    for name in ("src", "include", "lib", "工创赛控制", "test", "scripts"):
        copy_tree(src_root / name, dest / name)

    for name in (".editorconfig",):
        item = src_root / name
        if item.is_file():
            shutil.copy2(item, dest / name)


def strip_platformio(dest: Path) -> None:
    for name in ("platformio.ini", "extra_script.py"):
        path = dest / name
        if path.exists():
            path.unlink()


def write_project_files(dest: Path, keil_readme: bool) -> None:
    write_text(dest / "MDK-ARM" / "2027-control.uvprojx", uvprojx_text())
    write_text(dest / "MDK-ARM" / "2027-control.uvoptx", uvoptx_text())
    write_text(dest / ".gitignore", GITIGNORE)
    if keil_readme:
        write_text(dest / "README.md", keil_readme_text())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--hal-src",
        type=Path,
        default=default_hal_src(),
        help="带 Drivers/ 和 startup 的省赛工程目录",
    )
    parser.add_argument(
        "--dest",
        type=Path,
        help="输出目录。省略且加 --inplace 时改当前仓库",
    )
    parser.add_argument(
        "--inplace",
        action="store_true",
        help="在当前仓库里加上 Keil 工程，并去掉 PlatformIO 文件",
    )
    parser.add_argument(
        "--keil-readme",
        action="store_true",
        help="写成 keil 分支用的 README",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    src_root = repo_root()
    if args.inplace:
        dest = src_root
    elif args.dest is not None:
        dest = args.dest.resolve()
        dest.mkdir(parents=True, exist_ok=True)
        copy_app_tree(src_root, dest)
    else:
        print("请指定 --dest，或加上 --inplace。", file=sys.stderr)
        return 2

    (dest / "Core" / "Inc").mkdir(parents=True, exist_ok=True)
    (dest / "Core" / "Src").mkdir(parents=True, exist_ok=True)
    (dest / "MDK-ARM").mkdir(parents=True, exist_ok=True)

    copy_vendor(args.hal_src.resolve(), dest)
    write_project_files(dest, args.keil_readme or args.inplace)
    if args.inplace:
        strip_platformio(dest)

    print("Keil 工程已写到：%s" % dest)
    print("打开：%s" % (dest / "MDK-ARM" / "2027-control.uvprojx"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
