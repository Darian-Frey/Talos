# Talos client (Qt6 / C++) — D-006.
#
# Component layout mirrors ARCHITECTURE.md §5:
#   session/   F-214 connection & session manager (owns the socket)
#   protocol/  remote-protocol client (B1 now; B2 packet types later)
#   model/     F-215 per-frame, cycle/scanline-indexed event ring
#   view/      F-202..F-210 framebuffer + overlays and the register panels
#   app/       application shell / main window

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

qt_standard_project_setup()

add_executable(talos
  src/main.cpp
  src/app/MainWindow.cpp
  src/protocol/RdbClient.cpp
  src/session/HatariLauncher.cpp
  src/model/MachineState.cpp
  src/model/BlitterTrace.cpp
  src/model/DmaSndTrace.cpp
  src/model/RasterCodegen.cpp
  src/model/ScrollerCodegen.cpp
  src/model/Spectrum512.cpp
  src/model/StPicture.cpp
  src/model/GifWriter.cpp
  src/model/BorderCodegen.cpp
  src/model/SyncScroll.cpp
  src/model/MfpState.cpp
  src/view/FramebufferView.cpp
  src/view/BeamGeometry.cpp
  src/view/PaletteView.cpp
  src/view/BlitterTrafficView.cpp
  src/view/DmaSoundView.cpp
  src/view/RasterWorkspace.cpp
  src/view/ScrollerWorkspace.cpp
  src/view/Spectrum512View.cpp
  src/view/StPictureView.cpp
  src/view/ScanlineBudgetView.cpp
  src/view/BorderWalkthroughView.cpp
  src/view/SyncScrollView.cpp
  src/view/ReconstructView.cpp
  src/view/DisasmView.cpp
  src/view/MfpView.cpp
  src/view/AbCompareView.cpp
  src/view/CollapsibleDock.cpp
  src/capture/CaptureController.cpp
  src/capture/DisasmTracer.cpp
)

target_include_directories(talos PRIVATE src)

target_link_libraries(talos PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::Network
)

set_target_properties(talos PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
