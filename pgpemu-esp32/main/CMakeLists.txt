FILE(GLOB CSources *.c)
set(COMPONENT_SRCS ${CSources})
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
