# 03 — Development Environment (Windows)

*Checked 2026-11-08. Markers: **[doc]** verified in official docs or repos today. **[mem]** from prior knowledge, because devkitpro.org and dolphin-emu.org refused automated fetches today, so check these against the live page. **[exp]** confirm by doing.*

**Installed state (M1, 2026-11-08).** Everything in §1–5 is set up, and `template.dol` and `padtest.dol` both boot in Dolphin. What's actually installed, and what turned out different from the plan below:

| Item | Actual |
|---|---|
| devkitPro | Updater v3.0.3 → `C:\devkitPro`. devkitPPC r51 (GCC 16.2.0), libogc 3.2.1, wii-examples 20260510 |
| Libraries + GRRLIB | Installed by [`tools/setup-devkitpro.sh`](../tools/setup-devkitpro.sh) (re-runnable). GRRLIB v4.6.1+ (master) in the MSys2 home `~/GRRLIB`. **Its Makefile is in the inner `GRRLIB/` folder**, so the command is `make -C ~/GRRLIB/GRRLIB clean all install` (the README's `make -C GRRLIB` assumes you're in the clone's parent) |
| Official template | Installed by `wii-examples` at `C:\devkitPro\examples\wii\templates\makefile\application` (not `templates/makefile`). Built from a copy in `build-experiments/template/` (gitignored) |
| Dolphin | **2609a** portable 7z, unpacked to `C:\Users\haler\Dolphin-x64` with `portable.txt` (user folder = `Dolphin-x64\User`). winget only has 5.0 (2016), so don't use it. The first launch shows an *Allow Usage Statistics Reporting* dialog (answered No) |
| Build + run | `make -C tools/padtest run-dolphin` → [`tools/run-dolphin.sh`](../tools/run-dolphin.sh). It kills any open Dolphin, then boots the `.dol` with `-b -e` through `cmd /c start` so make returns at once. Edit → build → launch takes about 1 s. `DOLPHIN=/path/to/Dolphin.exe` overrides the default location |
| VS Code | `.vscode/tasks.json` (padtest: build / run in Dolphin / clean / send to Wii) and `c_cpp_properties.json` |

Paths in the rest of this guide that say `C:/Program Files/Dolphin` should read `C:/Users/haler/Dolphin-x64`.

---

## 0. What you need, in order

| # | Tool | Why | Source |
|---|---|---|---|
| 1 | **devkitPro installer** (with "Wii Development") | Compiler (devkitPPC), libogc, MSYS2 shell, pacman | https://github.com/devkitPro/installer/releases/latest (v3.0.3 is "Latest" **[doc]**). Instructions: https://devkitpro.org/wiki/Getting_Started |
| 2 | Extra pacman packages | PNG/fonts/audio decoders, libfat | devkitPro pacman |
| 3 | **GRRLIB** (built from source) | 2D graphics on GX | https://github.com/GRRLIB/GRRLIB |
| 4 | **Dolphin Emulator** (current stable or dev build) | Run and test without the Wii | https://dolphin-emu.org/download/ |
| 5 | **VS Code** (or Cursor, which you already have) + C/C++ extension | Editing, IntelliSense, build task | https://code.visualstudio.com/ |
| 6 | Git for Windows (you already have it: `/mingw64/bin/git`) | Version control | — |
| 7 | Python 3 (you already have 3.13) + `simfile` | A test oracle for the chart parser | `py -m pip install simfile` |

**C or C++?** Use **C++17 in a restrained style** (see doc 02). libogc and GRRLIB are C APIs and work from C++ unchanged.

**Windows or Linux?** Windows is fine. devkitPro ships a dedicated Windows installer with a bundled MSYS2 shell **[doc: devkitPro pacman page via search]**. There's no compelling reason to use WSL/Linux.

**Legacy toolchains:** Dance Clone was built with devkitPPC r24 / libogc 1.8.8 (2011–12). StepMania-Wii used similar-era tools. Those versions aren't installable through pacman, and **I don't recommend chasing them.** Use the current toolchain, and patch a *copy* of Dance Clone if you want it as a reference build (doc 07 §5).

---

## 1. Install devkitPro + devkitPPC + libogc

1. Download the **devkitPro Updater** `.exe` from https://github.com/devkitPro/installer/releases/latest **[doc]**. devkitPro asks people to follow https://devkitpro.org/wiki/Getting_Started for the details.
2. Run it (it needs an internet connection). When it asks for components, tick **Wii Development** (GameCube is optional). Keep the default install path `C:\devkitPro` **[mem]**.
3. From the Start menu, open **devkitPro → MSys2** **[doc via search summary]**. Use this shell for all builds.
4. Update and install the Wii group. In the MSys2 shell, **without** `sudo` (that's for Linux):

```bash
pacman -Syu
```

```bash
pacman -S --needed wii-dev
```

(If `pacman -Syu` updates pacman itself, close the shell, reopen it and run it again.)

5. Install the extra libraries we'll use:

```bash
pacman -S --needed libfat-ogc ppc-libpng ppc-freetype ppc-libjpeg-turbo ppc-zlib ppc-libmad ppc-libvorbisidec ppc-libogg
```

These package names come from devkitPro's `pacman-packages/ppc` tree and the GRRLIB README **[doc]**. If one is reported as "target not found", search for it with `pacman -Ss <name>`.

6. Verify:

```bash
echo $DEVKITPRO $DEVKITPPC
```

```bash
powerpc-eabi-gcc --version
```

```bash
pacman -Sg wii-dev
```

You should see `/opt/devkitpro` and `/opt/devkitpro/devkitPPC` (MSYS paths for `C:\devkitPro`) **[mem]**, a GCC version line, and the list of packages in the group (expect devkitPPC, libogc, gamecube-tools/elf2dol, wiiload, the Wii examples, …) **[exp]**.

## 2. Install GRRLIB

The `git` that MSys2 sees may be missing, so install it inside MSys2 first:

```bash
pacman -S --needed git make
```

```bash
cd ~ && git clone https://github.com/GRRLIB/GRRLIB.git
```

```bash
make -C ~/GRRLIB clean all install
```

The README lists these dependencies (installed in step 5) and the `make -C GRRLIB clean all install` command **[doc]**. It installs into the devkitPro portlibs, so projects link it with `-lgrrlib -lpngu -lpng -ljpeg -lfreetype -lfat -lz` **[exp: check the GRRLIB README for the exact link order of your version]**.

## 3. Prove the toolchain: build the official template (Milestone 1)

```bash
cd ~ && git clone https://github.com/devkitPro/wii-examples.git
```

```bash
cd ~/wii-examples/templates/makefile && make
```

Expected output: a `.dol` and `.elf` named after the folder **[exp]**. `wii-examples/templates/{makefile,cmake/application}` exist **[doc]**. We'll base our project Makefile on this template, *not* on the 2012 Makefiles in the reference projects.

---

## 4. Dolphin setup

### 4.1 Install

Download the current release from https://dolphin-emu.org/download/ (I couldn't confirm the version number today **[mem]**). Either the installer or the 7z "portable" build is fine. For a self-contained setup, put an empty `portable.txt` next to `Dolphin.exe` so the user folder lives next to the program **[mem]**.

### 4.2 Run homebrew directly (no Wii system files needed)

- **File → Open…** and pick `boot.dol` or `.elf`. Dolphin detects Wii vs GameCube executables automatically **[mem]**.
- Or from the command line (handy for a VS Code task):

```bash
"/c/Program Files/Dolphin/Dolphin.exe" -e "C:/Users/haler/Downloads/ddr-wii/game/boot.dol"
```

`-e`/`--exec` boots a file **[mem]**. Adjust the path to wherever you installed Dolphin.

**You don't need the Homebrew Channel inside Dolphin** for development. Loading the `.dol` directly is faster. (If you ever want HBC in Dolphin, wii.hacks.guide has a "homebrew-dolphin" page **[doc]**.)

### 4.3 Virtual SD card

Our game reads songs from `sd:/`. In Dolphin **[mem — confirm the labels in your build]**:

1. **Config → Wii** tab → *SD Card Settings*: tick **Insert SD Card**.
2. Tick **Enable SD Card Folder Sync** (or a similarly named option). Dolphin builds its virtual `sd.raw` image from a normal Windows folder at boot and writes changes back when emulation stops. The default folder is `<Dolphin User>\Load\WiiSDSync\`.
3. Put your SD layout there (see §6.3), e.g. `WiiSDSync\ddrwii\songs\...`.

If your build doesn't have folder sync, the fallback is the `sd.raw` image in `<User>\Wii\sd.raw`, mounted with a FAT tool. Folder sync is much easier.

### 4.4 Map the keyboard to the "dance pad"

1. **Controllers** → *GameCube Controllers* → Port 1 = **Standard Controller** → **Configure**.
2. Device: your keyboard (`DInput/0/Keyboard Mouse` or similar).
3. Bind **D-Pad Up/Down/Left/Right** to the arrow keys (or `W/S/A/D`, or four keys you can hit with two hands), **Start** to Enter, **A** to `X`, **B** to `Z` (the key), **Z trigger** to `C`.
4. Save as a profile, e.g. `DDR-Keyboard`.

**Note:** most keyboards limit how many keys can be held at once ("rollover"). If a three- or four-key combination doesn't register, it's the keyboard, not the game. **[exp: test opposite directions held together, see doc 04 §6]**.

Don't use Dolphin's **"Dance Mat"** device type for keyboard testing. It's modelled on the Family Trainer/Active Life mat layout, and Dolphin forum advice for DDR is to use *Standard Controller* **[doc: Dolphin forums via search]**.

### 4.5 (Optional) Real dance pad → PC → Dolphin

- Hardware: the **official Nintendo GameCube Controller Adapter for Wii U** (WUP-028) or a **Mayflash GC adapter switched to "Wii U" mode** **[mem]**.
- Windows driver: run **Zadig**, select "WUP-028", install **WinUSB** **[mem]**. Dolphin's guide is at https://dolphin-emu.org/docs/guides/how-use-official-gc-controller-adapter-wii-u/ (blocked for me today).
- Dolphin: Controllers → Port 1 = **GameCube Adapter for Wii U**.
- **Unknown [exp]:** whether the adapter passes a dance mat through cleanly. Mats seem to identify as standard controllers (doc 04), so it probably works. This is a nice-to-have. The authoritative test is the real Wii (§6).

### 4.6 Debugging in Dolphin

- **On-screen text** (most reliable). Our input test uses libogc's console (`console_init`) and `printf`.
- **Dolphin log window**: *View → Show Log* and *Show Log Configuration*. Enable the **OSReport**-type channels to see debug prints **[mem]**. libogc's mechanism for routing `stdout` to Dolphin/USB Gecko should be checked in the `devices/usbgecko` example and `libogc` headers **[exp]**.
- **Log to SD**: append to `sd:/ddrwii/log.txt` (works on both Dolphin and the Wii).
- **Crashes**: libogc prints a register dump/stack trace. Map addresses with `powerpc-eabi-addr2line -e boot.elf 0x8000xxxx`.
- Dolphin's debugger (*View → Code/Registers/Memory*, if enabled in Interface settings) can break into homebrew too **[mem]**.

### 4.7 One-command build-and-run

We'll add a `make run-dolphin` target (and a VS Code task) that rebuilds, copies `boot.dol` into place and launches `Dolphin.exe -e`. Edit → build → test is then about 10 seconds, with nothing copied to the Wii.

---

## 5. VS Code (or Cursor)

1. Install the **C/C++** extension (`ms-vscode.cpptools`). Makefile Tools is optional.
2. `.vscode/c_cpp_properties.json` (paths assume the default `C:/devkitPro`):

```json
{
  "configurations": [{
    "name": "Wii",
    "compilerPath": "C:/devkitPro/devkitPPC/bin/powerpc-eabi-g++.exe",
    "intelliSenseMode": "gcc-x86",
    "cppStandard": "c++17",
    "includePath": [
      "${workspaceFolder}/game/source/**",
      "C:/devkitPro/libogc/include",
      "C:/devkitPro/portlibs/wii/include",
      "C:/devkitPro/portlibs/ppc/include"
    ],
    "defines": ["GEKKO", "HW_RVL"]
  }],
  "version": 4
}
```

(`GEKKO`/`HW_RVL` are the macros the Wii toolchain predefines/uses. `intelliSenseMode` only affects the editor. **[exp]**: check the include directories exist after installing.)

3. `.vscode/tasks.json`. Build through the devkitPro MSYS2 bash so the environment is right:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "wii: build",
      "type": "shell",
      "command": "C:/devkitPro/msys2/usr/bin/bash.exe",
      "args": ["-lc", "cd \"$(cygpath '${workspaceFolder}')/game\" && make"],
      "group": { "kind": "build", "isDefault": true },
      "problemMatcher": "$gcc"
    },
    {
      "label": "wii: run in Dolphin",
      "type": "shell",
      "dependsOn": "wii: build",
      "command": "\"C:/Program Files/Dolphin/Dolphin.exe\" -e \"${workspaceFolder}/game/game.dol\""
    }
  ]
}
```

(`C:/devkitPro/msys2/usr/bin/bash.exe` is the expected location **[mem/exp]**.)

---

## 6. The real Wii

### 6.1 What's needed

- **The Homebrew Channel (HBC).** Yes, you need it (or an equivalent loader) to run unsigned `.dol` files.
- Follow **https://wii.hacks.guide/** **[doc]**. Its *Get Started* page picks the exploit for your console:
  - **Wilbrand** (uses the SD card; needs your Wii's MAC address and System Menu version),
  - **str2hax** (no SD card needed),
  - **ModMii** (PC-guided).
  - LetterBomb (System Menu 4.3 only) is now in the "legacy" list. It still works per its own page.
- All of them launch the **HackMii Installer**, which installs **HBC** and **BootMii** (as IOS, or as boot2 on early consoles).

### 6.2 Keeping it safe and reversible

| Step | Modifies NAND? | Recommendation |
|---|---|---|
| Run the exploit | No (it runs from SD or messages) | Required |
| Install HBC (HackMii) | Yes: installs one channel title | Required. It's the standard, widely used minimum, and can be uninstalled with HackMii |
| BootMii + **NAND backup** to SD | Backup: no. BootMii as IOS: installs an IOS slot | **Strongly recommended**, and do it immediately. Keep `nand.bin` + `keys.bin` safe on your PC |
| Priiloader | Yes: patches System Menu boot | The guide recommends it as brick protection. Your call, since it's not needed to run our game |
| cIOS, WADs, region changes, themes | Yes | **Don't**. Not needed for this project |

The setup SD card must be **FAT32 and ≤ 32 GB** for the exploits **[doc: LetterBomb page]**. SDHC up to 32 GB is the safe choice for the game too.

### 6.3 SD card layout for our game

```
SD:/
├── apps/
│   └── ddrwii/
│       ├── boot.dol          <- our build (HBC also accepts boot.elf, and prefers it)
│       ├── meta.xml          <- name, coder, version, description
│       └── icon.png          <- 128×48 PNG
└── ddrwii/
    ├── songs/
    │   └── <Pack>/<Song>/     <- StepMania layout: Song.sm or Song.ssc, song.ogg/.mp3, banner.png, bg.png
    ├── config.ini            <- global offset, scroll speed, …
    └── scores.dat            <- high scores
```

The `apps/<name>/boot.dol`, `meta.xml` and 128×48 `icon.png` layout is from WiiBrew's HBC page **[doc]**. Keeping songs outside `apps/` keeps the HBC folder small.

`meta.xml` example (the format follows Dance Clone's shipped one, `reference/extracted/danceclonewii/binary/DanceClone/meta.xml`):

```xml
<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<app version="1">
  <name>DDR Wii</name>
  <coder>you</coder>
  <version>0.1</version>
  <short_description>Four-panel rhythm game</short_description>
  <long_description>StepMania-chart rhythm game for GameCube dance pads.</long_description>
</app>
```

### 6.4 Getting builds onto the Wii

1. **SD card swap** (always works): copy `boot.dol` to `SD:/apps/ddrwii/` and launch from HBC.
2. **wiiload over Wi-Fi** (much faster): with HBC open on the Wii and the Wii on your network, in MSys2 run:

```bash
export WIILOAD=tcp:192.168.1.50
```

```bash
wiiload game.dol
```

Variable format and usage are per WiiBrew **[doc]**. Use your Wii's IP (shown in HBC **[mem]**, or in your router). The devkitPro template's `make run` target does the same. Allow it through the Windows firewall if asked. Note that wiiload-launched apps don't run from `apps/ddrwii/`, so the game should look for its data at the fixed `sd:/ddrwii/` path.

### 6.5 Testing the physical pad

Plug the mat into **GameCube port 1** (the leftmost, nearest the edge **[mem]**). Run the input-test program from doc 04 via HBC or wiiload. It shows each port's SI device type and live arrow state.

---

## 7. Git project structure

The workspace isn't a git repository yet. When you're ready (needs your OK):

```bash
cd /c/Users/haler/Downloads/ddr-wii && git init
```

Suggested `.gitignore`:

```
# build outputs
game/build/
*.dol
*.elf
*.map
# large reference material (keep originals out of git, or use Git LFS)
reference/
# local SD staging / songs (copyrighted music)
sd/ddrwii/songs/
```

The directory layout is in [06-technical-design.md §9](06-technical-design.md#9-initial-repository-structure).
