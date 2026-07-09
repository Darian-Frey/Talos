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

# As the app grows, add sources per component directory. For the pre-M0 scaffold
# this is a single translation unit that proves the toolchain and opens a window.
add_executable(talos
  src/main.cpp
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
