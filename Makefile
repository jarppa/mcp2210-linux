#
# Makefile for mcp2210
#

mcp2210-objs := mcp2210-core.o mcp2210-spi.o mcp2210-ctl.o

obj-$(CONFIG_MCP2210)		+= mcp2210.o
