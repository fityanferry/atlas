/*
 *  sec_audio_debug.c
 *
 *  Copyright (c) 2018 Samsung Electronics
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _SEC_AUDIO_DEBUG_H
#define _SEC_AUDIO_DEBUG_H

enum rmem_size_type {
	TYPE_ABOX_DBG_SIZE,
	TYPE_ABOX_SLOG_SIZE,
	TYPE_SIZE_MAX,
};

struct sec_audio_log_data {
	ssize_t buff_idx;
	int full;
	char *audio_log_buffer;
	ssize_t read_idx;
	size_t sz_log_buff;
	int virtual;
	char *name;
};

enum abox_debug_err_type {
	TYPE_ABOX_DATAABORT = 1,
	TYPE_ABOX_PREFETCHABORT,
	TYPE_ABOX_OSERROR,
	TYPE_ABOX_VSSERROR,
	TYPE_ABOX_UNDEFEXCEPTION,
	TYPE_ABOX_UNKNOWNERROR,
	TYPE_ABOX_DEBUG_MAX,
};

#ifdef CONFIG_SND_SOC_SAMSUNG_AUDIO

void abox_log_extra_copy(char *src_base, unsigned int index_reader,
					unsigned int index_writer, unsigned int src_buff_size);
void abox_log_extra_update(void);

int is_abox_rdma_enabled(int id);
int is_abox_wdma_enabled(int id);
void abox_debug_string_update(enum abox_debug_err_type type, void *addr);

void adev_err(struct device *dev, const char *fmt, ...);
void adev_warn(struct device *dev, const char *fmt, ...);
void adev_info(struct device *dev, const char *fmt, ...);
void adev_dbg(struct device *dev, const char *fmt, ...);

int register_debug_mixer(struct snd_soc_card *card);
int alloc_sec_audio_log(struct sec_audio_log_data *p_dbg_log_data, size_t buffer_len);

void sec_audio_log(int level, struct device *dev, const char *fmt, ...);
void sec_audio_bootlog(int level, struct device *dev, const char *fmt, ...);
void sec_audio_pmlog(int level, struct device *dev, const char *fmt, ...);

int check_upload_mode_disabled(void);
int check_debug_level_low(void);
size_t get_rmem_size_min(enum rmem_size_type id);

#ifdef CHANGE_DEV_PRINT
#ifdef dev_err
#undef dev_err
#endif
#define dev_err(dev, fmt, arg...)	adev_err(dev, fmt, ##arg)

#ifdef dev_warn
#undef dev_warn
#endif
#define dev_warn(dev, fmt, arg...)	adev_warn(dev, fmt, ##arg)

#ifdef dev_info
#undef dev_info
#endif
#define dev_info(dev, fmt, arg...)	adev_info(dev, fmt, ##arg)

#ifdef DEBUG
#ifdef dev_dbg
#undef dev_dbg
#endif
#define dev_dbg(dev, fmt, arg...)	adev_dbg(dev, fmt, ##arg)
#endif /* DEBUG */

#endif /* CHANGE_DEV_PRINT */

#else /* CONFIG_SND_SOC_SAMSUNG_AUDIO */
inline int is_abox_rdma_enabled(int id)
{
	return 0;
}
inline int is_abox_wdma_enabled(int id)
{
	return 0;
}
inline void abox_debug_string_update(enum abox_debug_err_type type, void *addr)
{}

inline int register_debug_mixer(struct snd_soc_card *card)
{
	return -EACCES;
}

inline int alloc_sec_audio_log(struct sec_audio_log_data *p_dbg_log_data, size_t buffer_len)
{
	return -EACCES;
}

inline void free_sec_audio_log(struct sec_audio_log_data *p_dbg_log_data)
{
}

inline void sec_audio_log(int level, struct device *dev, const char *fmt, ...)
{
}

inline void sec_audio_bootlog(int level, struct device *dev, const char *fmt, ...)
{
}

inline void sec_audio_pmlog(int level, struct device *dev, const char *fmt, ...)
{
}

inline int check_upload_mode_disabled(void)
{
	return -EACCES;
}

inline int check_debug_level_low(void);
{
	return -EACCES;
}

inline size_t get_rmem_size_min(enum rmem_size_type id)
{
	return -EACCES;
}

#endif /* CONFIG_SND_SOC_SAMSUNG_AUDIO */

#endif
