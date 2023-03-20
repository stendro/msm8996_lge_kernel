#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <linux/file.h>

#include "msm_sd.h"
#include "msm_tcs.h"
#include "msm_tcs_i2c.h"

int32_t TCSWrite16bit(uint32_t RamAddr, uint16_t RamData )
{
	int32_t ret = 0;
	ret = tcs_i2c_write(RamAddr,RamData, 2);
	return ret;
}

int32_t TCSRead16bit(uint32_t RamAddr, uint16_t *ReadData )
{
	int32_t ret = 0;
	ret =  tcs_i2c_read(RamAddr, ReadData, 2);
	return ret;
}

int32_t TCSSensorWrite32bit(uint32_t RamAddr, uint32_t RamData )
{
	int32_t ret = 0;
	uint8_t data[4];

	data[0] = (RamData >> 24) & 0xFF;
	data[1] = (RamData >> 16) & 0xFF;
	data[2] = (RamData >> 8)  & 0xFF;
	data[3] = (RamData) & 0xFF;

	ret =  tcs_i2c_write_seq(RamAddr, &data[0], 4);
	return ret;
}

int32_t TCSRead32bit(uint32_t RamAddr, uint32_t *ReadData )
{
	int32_t ret = 0;
	uint8_t buf[4];

	ret =  tcs_i2c_read_seq(RamAddr, buf, 4);
	*ReadData = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	return ret;
}

int32_t TCSWrite8bit(uint32_t RegAddr, uint8_t RegData)
{
	int32_t ret = 0;
	uint16_t data = (uint16_t)RegData;
	ret =  tcs_i2c_write(RegAddr, data, 1);
	return ret;
}

int32_t TCSRead8bit(uint32_t RegAddr, uint8_t *RegData)
{
	int32_t ret = 0;
	uint16_t data = 0;
	ret =  tcs_i2c_read(RegAddr, &data, 1);
	*RegData = (uint8_t)data;
	return ret;
}
