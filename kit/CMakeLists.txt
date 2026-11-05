# The shared panel kit: the look, the preset browser and the Bench, used by every
# plug-in in the range. Header-only apart from the two embedded fonts.
juce_add_binary_data(kit_fonts
  HEADER_NAME KitFonts.h
  NAMESPACE KitFonts
  SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/fonts/VT323-Regular.ttf
    ${CMAKE_CURRENT_LIST_DIR}/fonts/PressStart2P-Regular.ttf)
set_target_properties(kit_fonts PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(kit_fonts INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
