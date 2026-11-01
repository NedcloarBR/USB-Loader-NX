#---------------------------------------------------------------------------------
# USB Loader NX: compiles this repository's src/ into an Atmosphère module in place.
#
# A module's system_module.mk sets, then includes this file right after BINFILES:
#   USBL_SOURCES   directories under src/ (and their subdirectories) holding sources
#   USBL_DATA      directories under src/ embedded with bin2o
#   USBL_INCLUDES  extra include directories (absolute)
# and appends $(USBL_INCLUDE) to INCLUDE once that is defined.
#---------------------------------------------------------------------------------
USBL_ROOT        := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

USBL_SOURCE_DIRS := $(foreach dir,$(USBL_SOURCES),$(USBL_ROOT)/src/$(dir) $(call DIR_WILDCARD,$(USBL_ROOT)/src/$(dir)))
USBL_DATA_DIRS   := $(foreach dir,$(USBL_DATA),$(USBL_ROOT)/src/$(dir))

VPATH            +=  $(USBL_SOURCE_DIRS) $(USBL_DATA_DIRS)
CFILES           +=  $(call FIND_SOURCE_FILES,$(USBL_SOURCE_DIRS),c)
CPPFILES         +=  $(call FIND_SOURCE_FILES,$(USBL_SOURCE_DIRS),cpp)
SFILES           +=  $(call FIND_SOURCE_FILES,$(USBL_SOURCE_DIRS),s)
BINFILES         +=  $(foreach dir,$(USBL_DATA_DIRS),$(notdir $(wildcard $(dir)/*.*)))

USBL_INCLUDE     :=  -I$(USBL_ROOT)/common/include $(foreach dir,$(USBL_INCLUDES),-I$(dir))
