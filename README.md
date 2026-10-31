**MCDSi** is a homebrew port/demake of **Minecraft (1.7.10)** built exclusively for the **Nintendo DSi** (`DSi-Mode`). Written from scratch in C, this project leverages the DSi's enhanced hardware (133 MHz ARM9 CPU and 16 MB of RAM) to bring the classic block-building, crafting, and survival experience to dual-screen handheld hardware.

> ⚠️ **Important Compatibility Note:** Because this port relies on Nintendo DSi hardware resources, **it will NOT work on original Nintendo DS / DS Lite consoles or emulators that lack DSi-Mode support (such as DeSmuME)**.

## ✨ Features

* **World & Environment:** 3D voxel-based world rendering (`world.c`, `render.c`) featuring a dynamic day and night cycle (`daynight.c`).
* **Blocks & Items:** Classic block placement, destruction, item drops (`blocks.c`, `items.c`, `drops.c`), and 1.7.10-styled textures.
* **Interactive Tile Entities:** Functional **Chests** (single and double) and **Furnaces** for storage and smelting (`chest.c`, `furnace.c`).
* **Mobs & Entities:** Custom mob AI and 3D entity rendering (`mobs.c`, `render_mobs.c`).
* **Player Mechanics:** Full player movement, health, armor system, and inventory management (`player.c`).
* **Authentic UI & Audio:** Dual-screen user interface with hardware-accelerated elements (`ui.c`, `ui_hw.c`), classic main menu with blurred panorama background, splash texts (`menus.c`, `splash.c`), and custom-converted sound effects (`sfx.c`).

## 🛠️ Project Structure

* `source/` & `include/`: Core game engine and logic written in C.
* `data/`: Pre-converted binary assets (textures, UI, panoramas, fonts, and sound banks).
* `tools/`: Python utility scripts for asset pipeline conversion (`convert_assets.py`, `convert_sounds.py`, `pano_blur.py`, `gen_keyicons.py`).
* `Minecraft_DSi.nds`: Compiled DSi-Mode executable ROM.

## 🚀 How to Run

1. Download the latest `Minecraft_DSi.nds` from the repository or Releases page.
2. **On Real Hardware (DSi / 3DS):** Copy the `.nds` file to your SD card and launch it via **TWiLight Menu++** or **Unlaunch** with **DSi Mode** enabled in the per-game settings. *(Note: Standard DS flashcards running in DS-Mode are not supported unless they support DSi hybrid/enhanced mode).*
3. **On Emulator:** Use **melonDS** (ensure `Console type` is set to **DSi** in *Config -> Emu settings*) or **No$gba** configured in DSi mode. **DeSmuME is not supported.**

## 🔧 Building from Source

To compile the project yourself, you will need a Nintendo DS/DSi development environment (**devkitARM / libnds** or **BlocksDS**) and **Python 3** (for asset conversion scripts):

```bash
git clone [https://github.com/YOUR_USERNAME/mcdsi.git](https://github.com/YOUR_USERNAME/mcdsi.git)
cd mcdsi
make
