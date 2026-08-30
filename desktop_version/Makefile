TARGET = vvvvvv

PSPSDK=$(shell psp-config --pspsdk-path)
PSPDEV = $(HOME)/pspdev

INCDIR = \
  $(PSPSDK)/include \
  $(PSPDEV)/psp/include \
  $(PSPDEV)/psp/include/SDL2 \
  ../third_party/ \
  ../third_party/SheenBidi/Headers \
  ../third_party/FAudio/src \
  ../third_party/physfs/extras \

OBJS = \
  src/BinaryBlob.o \
  src/BlockV.o \
  src/ButtonGlyphs.o \
  src/CustomLevels.o \
  src/CWrappers.o \
  src/DeferCallbacks.o \
  src/Editor.o \
  src/Ent.o \
  src/Entity.o \
  src/FileSystemUtils.o \
  src/Finalclass.o \
  src/Font.o \
  src/FontBidi.o \
  src/Game.o \
  src/GlitchrunnerMode.o \
  src/GOGNetwork.o \
  src/Graphics.o \
  src/GraphicsResources.o \
  src/GraphicsUtil.o \
  src/IMERender.o \
  src/Input.o \
  src/InterimVersion.in.o \
  src/KeyPoll.o \
  src/Labclass.o \
  src/LevelDebugger.o \
  src/Localization.o \
  src/LocalizationMaint.o \
  src/LocalizationStorage.o \
  src/lodepng_wrapper.o \
  src/Logic.o \
  src/main.o \
  src/Map.o \
  src/Music.o \
  src/Network.o \
  src/Otherlevel.o \
  src/preloader.o \
  src/Render.o \
  src/RenderFixed.o \
  src/RoomnameTranslator.o \
  src/Screen.o \
  src/Script.o \
  src/Scripts.o \
  src/Spacestation2.o \
  src/TerminalScripts.o \
  src/Textbook.o \
  src/Textbox.o \
  src/ThirdPartyDeps.o \
  src/Tower.o \
  src/UTF8.o \
  src/UtilityClass.o \
  src/VFormat.o \
  src/Vlogging.o \
  src/WarpClass.o \
  src/XMLUtils.o \
  src/Xoshiro.o \
  ../third_party/c-hashmap/map.o \
  ../third_party/physfs/extras/physfsrwops.o \
  ../third_party/SheenBidi/Source/BidiChain.o \
  ../third_party/SheenBidi/Source/BidiChain.o \
  ../third_party/SheenBidi/Source/BidiTypeLookup.o \
  ../third_party/SheenBidi/Source/BracketQueue.o \
  ../third_party/SheenBidi/Source/GeneralCategoryLookup.o \
  ../third_party/SheenBidi/Source/IsolatingRun.o \
  ../third_party/SheenBidi/Source/LevelRun.o \
  ../third_party/SheenBidi/Source/Object.o \
  ../third_party/SheenBidi/Source/PairingLookup.o \
  ../third_party/SheenBidi/Source/RunQueue.o \
  ../third_party/SheenBidi/Source/SBAlgorithm.o \
  ../third_party/SheenBidi/Source/SBBase.o \
  ../third_party/SheenBidi/Source/SBCodepoint.o \
  ../third_party/SheenBidi/Source/SBCodepointSequence.o \
  ../third_party/SheenBidi/Source/SBLine.o \
  ../third_party/SheenBidi/Source/SBLog.o \
  ../third_party/SheenBidi/Source/SBMirrorLocator.o \
  ../third_party/SheenBidi/Source/SBParagraph.o \
  ../third_party/SheenBidi/Source/SBScriptLocator.o \
  ../third_party/SheenBidi/Source/ScriptLookup.o \
  ../third_party/SheenBidi/Source/ScriptStack.o \
  ../third_party/SheenBidi/Source/SheenBidi.o \
  ../third_party/SheenBidi/Source/StatusStack.o \

CFLAGS = -G0 -O3 -fomit-frame-pointer -fsingle-precision-constant -fno-math-errno -ffast-math -Wall -Wno-trigraphs
# CFLAGS = -g3 -Wl,-Map,EBOOT.map
CXXFLAGS = $(CFLAGS) -fno-rtti
ASFLAGS = $(CFLAGS) -c

BUILD_PRX = 1
PSP_FW_VERSION = 660

LIBS = -lpspgu -lpspge -lpspdisplay -lpsppower -lpspctrl -lpspaudio -lpspatrac3 -lpspvram -lpng -lfreetype \
  -lbz2 -lz -lphysfs -lvorbis -lvorbisidec -lSDL2main -lSDL2_image -lSDL2 -lSDL2_mixer -lstdc++ -lGL -logg -lmodplug -lpspvfpu -lpsphprm -lFAudio -ltinyxml2

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = VVVVVV
# PSP_EBOOT_ICON = res/ICON0.PNG
# PSP_EBOOT_PIC1 = res/PIC1.PNG
# PSP_EBOOT_SND0 = res/SND0.AT3

include $(PSPSDK)/lib/build.mak

all: EBOOT.PBP copy

copy: EBOOT.PBP
	mkdir -p build
	mv EBOOT.PBP build/EBOOT.PBP
