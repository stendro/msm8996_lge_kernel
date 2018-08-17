/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 * Sensitive Data Protection
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/memcontrol.h>
#include <linux/atomic.h>

#include "ecryptfs_kernel.h"

extern spinlock_t inode_sb_list_lock;
static int ecryptfs_mm_debug = 1;
DEFINE_MUTEX(ecryptfs_mm_mutex);

struct ecryptfs_mm_drop_cache_param {
	int storage_id;
};

#define INVALIDATE_MAPPING_RETRY_CNT 3

static unsigned long invalidate_mapping_pages_retry(struct address_space *mapping,
		pgoff_t start, pgoff_t end, int retries) {
	unsigned long ret = 0;

	if(ecryptfs_mm_debug)
        SDP_LOGD("freeing [%s] sensitive inode[mapped pagenum = %lu]\n",
				mapping->host->i_sb->s_type->name,
				mapping->nrpages);
retry:
	ret = invalidate_mapping_pages(mapping, start, end);
	if(ecryptfs_mm_debug)
        SDP_LOGD("invalidate_mapping_pages ret = %lu, [%lu] remained\n",
				ret, mapping->nrpages);

	if(mapping->nrpages != 0) {
		if(retries > 0) {
			SDP_LOGD("[%lu] mapped pages remained in sensitive inode, retry..\n",
					mapping->nrpages);
			retries--;
			msleep(100);
			goto retry;
		}
	}

	return ret;
}
#if 1
#define PSEUDO_KEY_LEN 32
const char pseudo_key[PSEUDO_KEY_LEN] = {
        // PSEUDOSDP
        0x50, 0x53, 0x55, 0x45, 0x44, 0x4f, 0x53, 0x44,
        0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void ecryptfs_clean_sdp_key(struct ecryptfs_crypt_stat *crypt_stat)
{
    int rc = 0;

	SDP_LOGD("%s()\n", __func__);
	if((crypt_stat->tfm) && (crypt_stat->flags & ECRYPTFS_KEY_SET)) {
	    mutex_lock(&crypt_stat->cs_tfm_mutex);
        rc = crypto_ablkcipher_setkey(crypt_stat->tfm, pseudo_key,
                PSEUDO_KEY_LEN);
        if (rc) {
            SDP_LOGE("Error cleaning tfm rc = [%d]\n", rc);
        }
	    mutex_unlock(&crypt_stat->cs_tfm_mutex);
	}

	memset(crypt_stat->key, 0, ECRYPTFS_MAX_KEY_BYTES);
	crypt_stat->flags &= ~(ECRYPTFS_KEY_SET);
	crypt_stat->flags &= ~(ECRYPTFS_KEY_VALID);
}
#endif

void ecryptfs_mm_do_sdp_cleanup(struct inode *inode) {
	struct ecryptfs_crypt_stat *crypt_stat;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat = NULL;
	struct ecryptfs_inode_info *inode_info;
	struct sdp_user *tmp_user = NULL;
	int user_id = 0;

	crypt_stat = &ecryptfs_inode_to_private(inode)->crypt_stat;
	mount_crypt_stat = &ecryptfs_superblock_to_private(inode->i_sb)->mount_crypt_stat;
	inode_info = ecryptfs_inode_to_private(inode);

	user_id = sdp_get_user_id_by_storage_id(crypt_stat->storage_id);
	if (user_id < 0) {
		SDP_LOGE("%d storage is not in the SDP list", crypt_stat->storage_id);
		return;
	}

	tmp_user = sdp_get_user_by_user_id(user_id);
	if (tmp_user == NULL) {
		SDP_LOGE("%d user is not in the list", user_id);
		return;
	}

	if(crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE) {
		int rc = 0;
		if(S_ISDIR(inode->i_mode)) {
			SDP_LOGD("%s: inode: %p is dir, return\n",__func__, inode);
			return;
		}

		SDP_LOGD("%s: inode: %p  clean up start\n",__func__, inode);
		rc = vfs_fsync(inode_info->lower_file, 0);
		if(rc)
			SDP_LOGE("%s: vfs_sync returned error rc: %d\n", __func__, rc);

		//TODO: impletement getting locked state
		if(sdp_is_storage_locked(tmp_user, crypt_stat->storage_id)) {
			SDP_LOGD("%s: persona locked inode: %lu useid: %d\n",__func__, inode->i_ino, crypt_stat->storage_id);
			invalidate_mapping_pages_retry(inode->i_mapping, 0, -1, 3);
			ecryptfs_clean_sdp_key(crypt_stat);
		}

		SDP_LOGD("%s: inode: %p clean up stop\n",__func__, inode);
	}
}

static unsigned long drop_inode_pagecache(struct inode *inode) {
	int rc = 0;

	spin_lock(&inode->i_lock);

	if(ecryptfs_mm_debug)
		SDP_LOGD("%s() cleaning [%s] pages: %lu\n", __func__,
				inode->i_sb->s_type->name,inode->i_mapping->nrpages);

	if ((inode->i_mapping->nrpages == 0)) {
		spin_unlock(&inode->i_lock);
		SDP_LOGD("%s inode having zero nrpages\n", __func__);
		return 0;
	}

	spin_unlock(&inode->i_lock);

	/*
	 * flush mapped dirty pages.
	 */
	rc = filemap_write_and_wait(inode->i_mapping);
	if(rc)
		SDP_LOGE("filemap_flush failed, rc=%d\n", rc);

	if (inode->i_mapping->nrpages != 0)
		lru_add_drain_all();

	rc = invalidate_mapping_pages_retry(inode->i_mapping, 0, -1,
			INVALIDATE_MAPPING_RETRY_CNT);

	if(inode->i_mapping->nrpages)
			SDP_LOGE("%s() uncleaned [%s] pages: %lu\n", __func__,
					inode->i_sb->s_type->name,inode->i_mapping->nrpages);

	return rc;
}

static void ecryptfs_mm_drop_pagecache(struct super_block *sb, void *arg)
{
    int rc = 0;
	struct inode *inode;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;

	if(strcmp("ecryptfs", sb->s_type->name)) {
		SDP_LOGE("%s sb:%s is not ecryptfs superblock\n", __func__,
				sb->s_type->name);
		return;
	}

	mount_crypt_stat = &ecryptfs_superblock_to_private(sb)->mount_crypt_stat;

	SDP_LOGD("%s start() sb:%s\n", __func__, sb->s_type->name);

	spin_lock(&inode_sb_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list)
	{
		spin_lock(&inode->i_lock);
		if (inode->i_mapping && (inode->i_mapping->nrpages == 0)) {
			struct ecryptfs_crypt_stat *crypt_stat;
			spin_unlock(&inode->i_lock);
			spin_unlock(&inode_sb_list_lock);

			if(ecryptfs_mm_debug)
				SDP_LOGD("%s() ecryptfs inode [ino:%lu]\n",__func__, inode->i_ino);

			crypt_stat = &ecryptfs_inode_to_private(inode)->crypt_stat;

			if(crypt_stat && (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE)
					&& !atomic_read(&ecryptfs_inode_to_private(inode)->lower_file_count)) {
				ecryptfs_clean_sdp_key(crypt_stat);
			}
			spin_lock(&inode_sb_list_lock);
			continue;
		}
		spin_unlock(&inode->i_lock);
		spin_unlock(&inode_sb_list_lock);

		if(ecryptfs_mm_debug)
			SDP_LOGD("inode number: %lu i_mapping: %p [%s]\n",inode->i_ino,
					inode->i_mapping,inode->i_sb->s_type->name);
		if(inode->i_mapping
				&& !atomic_read(&ecryptfs_inode_to_private(inode)->lower_file_count)) {

			struct ecryptfs_crypt_stat *crypt_stat;
			rc = drop_inode_pagecache(inode);
            if (rc < 0) {
                SDP_LOGE("inode number : %lu, failed to drop inode pagecache", inode->i_ino);
                return;
            }

			if(ecryptfs_mm_debug)
					SDP_LOGD("lower inode: %p lower inode: %p nrpages: %lu\n",ecryptfs_inode_to_lower(inode),
							ecryptfs_inode_to_private(inode), ecryptfs_inode_to_lower(inode)->i_mapping->nrpages);

			crypt_stat = &ecryptfs_inode_to_private(inode)->crypt_stat;
            if (crypt_stat && (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE)) {
				ecryptfs_clean_sdp_key(crypt_stat);
            }
		}
		spin_lock(&inode_sb_list_lock);
	}
	spin_unlock(&inode_sb_list_lock);
	SDP_LOGD("%s:%d end \n", __func__, __LINE__);
}

static int ecryptfs_mm_task(void *arg)
{
	struct file_system_type *type;
	struct ecryptfs_mm_drop_cache_param *param = arg;

	type = get_fs_type("ecryptfs");

	if(type) {
		if(ecryptfs_mm_debug)
			SDP_LOGD("%s type name: %s flags: %d\n", __func__, type->name, type->fs_flags);

		mutex_lock(&ecryptfs_mm_mutex);
		iterate_supers_type(type,ecryptfs_mm_drop_pagecache, param);
		mutex_unlock(&ecryptfs_mm_mutex);

		put_filesystem(type);
	}

	kfree(param);
	return 0;
}

void ecryptfs_mm_drop_cache(int storage_id) {
	struct task_struct *task;
	struct ecryptfs_mm_drop_cache_param *param =
			kzalloc(sizeof(*param), GFP_KERNEL);

	if (!param) {
		SDP_LOGE("%s :: skip. no memory to alloc param\n", __func__);
		return;
	}
	param->storage_id = storage_id;

	SDP_LOGD("running cache cleanup thread - sdp-storage_id : %d\n", storage_id);
	task = kthread_run(ecryptfs_mm_task, param, "sdp_cached");

	if (IS_ERR(task)) {
		SDP_LOGE("SDP : unable to create kernel thread: %ld\n", PTR_ERR(task));
	}
}

#ifdef SDP_DEBUG
/*
 * This dump will appear in ramdump
 */
static void __page_dump(unsigned char *buf, int len, const char* str)
{
	unsigned int i = 0;
	char s[512] = {0,};

	s[0] = 0;
	for(i=0;i<len && i<16;++i) {
		char tmp[8];
		sprintf(tmp, " %02x", buf[i]);
		strcat(s, tmp);
	}

	if (len > 16) {
		char tmp[8];
		sprintf(tmp, " ...");
		strcat(s, tmp);
	}

	SDP_LOGD("%s [%s len=%d]\n", s, str, len);
}

void page_dump (struct page *p) {
	void *d;
	d = kmap_atomic(p);
	if(d) {
		__page_dump((unsigned char *)d, PAGE_SIZE, "freeing");
		kunmap_atomic(d);
	}
}
#else
void page_dump (struct page *p) {
	// Do nothing
}
#endif
