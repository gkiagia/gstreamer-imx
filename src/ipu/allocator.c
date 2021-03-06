/* Allocation functions for physical memory
 * Copyright (C) 2013  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include "allocator.h"
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/ipu.h>
#include "../common/phys_mem_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_ipu_allocator_debug);
#define GST_CAT_DEFAULT imx_ipu_allocator_debug



static void gst_imx_ipu_allocator_finalize(GObject *object);

static gboolean gst_imx_ipu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
static gboolean gst_imx_ipu_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
static gpointer gst_imx_ipu_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags);
static void gst_imx_ipu_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);


G_DEFINE_TYPE(GstImxIpuAllocator, gst_imx_ipu_allocator, GST_TYPE_IMX_PHYS_MEM_ALLOCATOR)




GstAllocator* gst_imx_ipu_allocator_new(int ipu_fd)
{
	GstAllocator *allocator;
	allocator = g_object_new(gst_imx_ipu_allocator_get_type(), NULL);

	GST_IMX_IPU_ALLOCATOR(allocator)->fd = ipu_fd;

	return allocator;
}


static gboolean gst_imx_ipu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size)
{
	dma_addr_t m;
	int ret;
	GstImxIpuAllocator *ipu_allocator = GST_IMX_IPU_ALLOCATOR(allocator);

	m = (dma_addr_t)size;
	ret = ioctl(ipu_allocator->fd, IPU_ALLOC, &m);
	memory->cpu_addr = 0;
	if (ret < 0)
	{
		GST_ERROR("could not allocate %u bytes of physical memory: %s", size, strerror(errno));
		memory->phys_addr = 0;
		return FALSE;
	}
	else
	{
		GST_DEBUG("allocated %u bytes of physical memory at address 0x%x", size, m);
		memory->phys_addr = m;
		return TRUE;
	}
}


static gboolean gst_imx_ipu_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	dma_addr_t m;
	int ret;
	GstImxIpuAllocator *ipu_allocator = GST_IMX_IPU_ALLOCATOR(allocator);

	m = (dma_addr_t)(memory->phys_addr);
	ret = ioctl(ipu_allocator->fd, IPU_FREE, &m);
	if (ret < 0)
	{
		GST_ERROR_OBJECT(allocator, "could not free physical memory at address 0x%x: %s", m, strerror(errno));
		return FALSE;
	}
	else
	{
		GST_DEBUG_OBJECT(allocator, "freed physical memory at address 0x%x", m);
		return TRUE;
	}
}


static gpointer gst_imx_ipu_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags)
{
	int prot = 0;
	GstImxPhysMemory *phys_mem = (GstImxPhysMemory *)memory;
	GstImxIpuAllocator *ipu_allocator = GST_IMX_IPU_ALLOCATOR(allocator);

	// TODO: refcount for mapping
	if (phys_mem->mapped_virt_addr != NULL)
		return phys_mem->mapped_virt_addr;

	if (flags & GST_MAP_READ)
		prot |= PROT_READ;
	if (flags & GST_MAP_WRITE)
		prot |= PROT_WRITE;

	phys_mem->mapped_virt_addr = mmap(0, size, prot, MAP_SHARED, ipu_allocator->fd, (dma_addr_t)(phys_mem->phys_addr));
	if (phys_mem->mapped_virt_addr == MAP_FAILED)
	{
		phys_mem->mapped_virt_addr = NULL;
		GST_ERROR_OBJECT(ipu_allocator, "memory-mapping the IPU framebuffer failed: %s", strerror(errno));
		return NULL;
	}

	return phys_mem->mapped_virt_addr;
}


static void gst_imx_ipu_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	if (memory->mapped_virt_addr != NULL)
	{
		if (munmap(memory->mapped_virt_addr, memory->mem.maxsize) == -1)
			GST_ERROR_OBJECT(allocator, "unmapping memory-mapped IPU framebuffer failed: %s", strerror(errno));
		memory->mapped_virt_addr = NULL;
	}
}




static void gst_imx_ipu_allocator_class_init(GstImxIpuAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstImxPhysMemAllocatorClass *parent_class = GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_ipu_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_ipu_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_imx_ipu_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_imx_ipu_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_ipu_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(imx_ipu_allocator_debug, "imxipuallocator", 0, "Freescale i.MX IPU physical memory/allocator");
}


static void gst_imx_ipu_allocator_init(GstImxIpuAllocator *allocator)
{
	GstAllocator *base = GST_ALLOCATOR(allocator);
	base->mem_type = GST_IMX_IPU_ALLOCATOR_MEM_TYPE;
}


static void gst_imx_ipu_allocator_finalize(GObject *object)
{
	GST_DEBUG_OBJECT(object, "shutting down IMX IPU allocator");
	G_OBJECT_CLASS(gst_imx_ipu_allocator_parent_class)->finalize(object);
}

