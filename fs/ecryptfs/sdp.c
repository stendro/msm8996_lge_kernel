#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/stat.h>
#include <linux/device.h>
#include <asm/unaligned.h>

#include "ecryptfs_kernel.h"
#include "sdp_ioctl.h"
#include "mm.h"

#define SDP_STATE_UNLOCKED 0
#define SDP_STATE_LOCKED 1

#define SDP_CMD_ENC_FEK 1
#define SDP_CMD_DEC_EFEK 2
#define SDP_CMD_RES_EFEK 3
#define SDP_CMD_RES_FEK 4

#define SDP_CMD_RES_GOOD 1
#define SDP_CMD_RES_BAD 2

#define SDP_KEY_TYPE_AES 0
#define SDP_KEY_TYPE_ECDH 1

int g_locked_state = 1;

struct kmem_cache *ecryptfs_sdp_user_cache;
struct kmem_cache *ecryptfs_sdp_storage_cache;

static struct list_head sdp_user_list;
struct mutex sdp_user_list_mutex;
struct sdp_user *current_sdp_user;

static int zero_out(char *buf, unsigned int len) {
	char zero = 0;
	int retry_cnt = 3;
	int i;

	retry:
	if(retry_cnt > 0) {
		memset((void *)buf, zero, len);
		for (i = 0; i < len; ++i) {
			zero |= buf[i];
			if (zero) {
				SDP_LOGE("the memory was not properly zeroed\n");
				retry_cnt--;
				goto retry;
			}
		}
	} else {
		SDP_LOGE("FATAL : can't zero out!!\n");
		return -1;
	}

	return 0;
}

static int sdp_init_user_list(void)
{
	INIT_LIST_HEAD(&sdp_user_list);
	mutex_init(&sdp_user_list_mutex);
	return 0;
}

struct sdp_user *sdp_get_current_user(void)
{
	return current_sdp_user;
}

int sdp_set_curret_user(uid_t user_id)
{
	struct sdp_user *user, *prev_user;
	struct sdp_storage *storage;
	uid_t uid = current_uid();
	if (uid != 1000 && uid != 0) {
		SDP_LOGE("Setting sdp user is not permitted:uid(%d)\n", uid);
		return -1;
	}
	if (current_sdp_user->user_id == user_id) {
		SDP_LOGD("current user is already %d\n", user_id);
		return 0;
	}
	prev_user = current_sdp_user;
	//TODO:: lock sdp storage previous unlocked storage
	mutex_lock(&sdp_user_list_mutex);
	list_for_each_entry(user, &sdp_user_list, list) {
		if (user->user_id == user_id) {
			current_sdp_user = user;
			mutex_unlock(&sdp_user_list_mutex);
			list_for_each_entry(storage, &prev_user->storage_list, list) {
				if (storage->lock_state == SDP_STATE_UNLOCKED) {
					//memset(storage->sdpk.data, 0, storage->sdpk->len);
					zero_out(storage->sdpk->data, storage->sdpk->len);
					kfree(storage->sdpk);
					storage->sdpk = NULL;
					storage->lock_state = SDP_STATE_LOCKED;
				}
			}
			SDP_LOGD("current user is set to %d\n", user_id);
			return 0;
		}
	}
	mutex_unlock(&sdp_user_list_mutex);
	SDP_LOGE("user_id(%d) is not in the list\n", user_id);
	return -1;
}

int sdp_user_add(uid_t user_id)
{
	struct sdp_user *user;
	struct sdp_user *tmp_user;
    uid_t uid;
    uid = current_uid();
    if (uid != 1000 && uid != 0) {
        SDP_LOGE("Adding sdp user is not permitted:uid(%d)\n", uid);
        return -1;
    }
	//TODO: check a user is already exist
	list_for_each_entry(tmp_user, &sdp_user_list, list) {
		if (tmp_user->user_id == user_id) {
			SDP_LOGD("user %d is already exist\n", user_id);
			return 1;
		}
	}
	user = kmem_cache_zalloc(ecryptfs_sdp_user_cache, GFP_KERNEL);
	if (!user) {
		SDP_LOGE("Error allocating from ecryptfs_sdp_user_cache\n");
		return -ENOMEM;
	}
	user->user_id = user_id;
	INIT_LIST_HEAD(&user->storage_list);
	mutex_init(&user->storage_list_mutex);

	mutex_lock(&sdp_user_list_mutex);
	list_add_tail(&user->list, &sdp_user_list);
	mutex_unlock(&sdp_user_list_mutex);

	if (current_sdp_user == NULL) {
		current_sdp_user = user;
	}
	return 0;
}
static void sdp_storage_free(struct sdp_storage *storage) {
	storage->lock_state = SDP_STATE_LOCKED;
	if (storage->sdpk) {
		memset(storage->sdpk->data, 0, SDP_KEY_SIZE_MAX);
		kfree(storage->sdpk);
		storage->sdpk = NULL;
	}
}
int sdp_user_del(uid_t user_id)
{
	struct sdp_user *user, *next_user;
	struct sdp_storage *tmp_storage, *next;
	uid_t uid = current_uid();
	if (uid != 1000 && uid != 0) {
		SDP_LOGE("Deleting sdp user is not permitted:uid(%d)\n", uid);
		return -1;
	}
	mutex_lock(&sdp_user_list_mutex);
	list_for_each_entry_safe(user, next_user, &sdp_user_list, list) {
		if (user->user_id == user_id) {
			mutex_lock(&user->storage_list_mutex);
			list_for_each_entry_safe(tmp_storage, next, &user->storage_list, list) {
				sdp_storage_free(tmp_storage);
				list_del(&tmp_storage->list);
				kmem_cache_free(ecryptfs_sdp_storage_cache, tmp_storage);
			}
			mutex_unlock(&user->storage_list_mutex);
			list_del(&user->list);
			mutex_unlock(&sdp_user_list_mutex);
			kmem_cache_free(ecryptfs_sdp_user_cache, user);
			SDP_LOGD("current user is set to %d\n", user_id);
			return 0;
		}
	}
	mutex_unlock(&sdp_user_list_mutex);
	SDP_LOGD("user_id(%d) is not in the list\n", user_id);
	return 0;
}

int sdp_storage_add(int user_id, int storage_id, struct sdp_key *protected_key) {
	struct sdp_user *user;
	struct sdp_storage *new_storage;
	struct sdp_storage *tmp_storage;

	new_storage = kmem_cache_zalloc(ecryptfs_sdp_storage_cache, GFP_KERNEL);
	if (!new_storage) {
		SDP_LOGE("Error allocating from ecryptfs_sdp_storage_cache\n");
		return -ENOMEM;
	}
	new_storage->storage_id = storage_id;
	new_storage->sdpk = NULL;
	new_storage->protected_key = protected_key;
	new_storage->lock_state = SDP_STATE_LOCKED;

	list_for_each_entry(user, &sdp_user_list, list) {
		if (user->user_id == user_id) {
			list_for_each_entry(tmp_storage, &user->storage_list, list) {
				if (tmp_storage->storage_id == storage_id) {
					SDP_LOGD("Storage id %d is already exist\n", storage_id);
					return 0;
				}
			}
			mutex_lock(&user->storage_list_mutex);
			list_add_tail(&new_storage->list, &user->storage_list);
			mutex_unlock(&user->storage_list_mutex);
		}
	}
	return 0;
}

int sdp_storage_del(int user_id, int storage_id) {
	struct sdp_user *user;
	struct sdp_storage *tmp_storage, *next;

	list_for_each_entry(user, &sdp_user_list, list) {
		if (user->user_id == user_id) {
			list_for_each_entry_safe(tmp_storage, next, &user->storage_list, list) {
				if (tmp_storage->storage_id == storage_id) {
					mutex_lock(&user->storage_list_mutex);
					list_del(&tmp_storage->list);
					mutex_unlock(&user->storage_list_mutex);
					sdp_storage_free(tmp_storage);
					memset(tmp_storage->protected_key->data, 0, tmp_storage->protected_key->len);
					kfree(tmp_storage->protected_key);
					kmem_cache_free(ecryptfs_sdp_storage_cache, tmp_storage);
					SDP_LOGD("sdp storage %d is removed\n", storage_id);
					return 0;
				}
			}
		}
	}
	SDP_LOGD("sdp storage %d is not exist\n", storage_id);
	return 0;
}
int sdp_storage_lock(struct sdp_user *user, int storage_id)
{
	struct sdp_storage *tmp_storage;
	uid_t uid = current_uid();
	if (uid != 1000 && uid != 0) {
		SDP_LOGE("Locking sdp user is not permitted:uid(%d)\n", uid);
		return -1;
	}
	mutex_lock(&user->storage_list_mutex);
	list_for_each_entry(tmp_storage, &user->storage_list, list) {
		if (tmp_storage->storage_id == storage_id) {
			sdp_storage_free(tmp_storage);
			mutex_unlock(&user->storage_list_mutex);
			SDP_LOGD("sdp storage(%d) is locked\n", storage_id);
			return 0;
		}
	}
	mutex_unlock(&user->storage_list_mutex);
	SDP_LOGD("sdp storage(%d) is not in the list\n", storage_id);
	return 0;
}

int sdp_storage_unlock(struct sdp_user *user, int storage_id, struct sdp_key *key)
{
	struct sdp_storage *tmp_storage;
	uid_t uid = current_uid();
	if (uid != 1000 && uid != 0) {
		SDP_LOGE("Locking sdp user is not permitted:uid(%d)\n", uid);
		return -1;
	}

	mutex_lock(&user->storage_list_mutex);
	list_for_each_entry(tmp_storage, &user->storage_list, list) {
		if (tmp_storage->storage_id == storage_id) {
			if (tmp_storage->lock_state == SDP_STATE_UNLOCKED) {
				SDP_LOGD("sdp storage(%d) is already unlocked\n", storage_id);
				mutex_unlock(&user->storage_list_mutex);
				return 0;
			}
			tmp_storage->lock_state = SDP_STATE_UNLOCKED;
			tmp_storage->sdpk = key;
			mutex_unlock(&user->storage_list_mutex);
			SDP_LOGD("sdp storage(%d) is unlocked\n", storage_id);
			return 0;
		}
	}
	mutex_unlock(&user->storage_list_mutex);
	SDP_LOGD("sdp storage(%d) is not in the list\n", storage_id);
	return 0;
}

int sdp_is_storage_locked(struct sdp_user *user, int storage_id)
{

	struct sdp_storage *tmp_storage;

	list_for_each_entry(tmp_storage, &user->storage_list, list) {
		if (tmp_storage->storage_id == storage_id) {
			if (tmp_storage->lock_state == SDP_STATE_UNLOCKED) {
				SDP_LOGD("sdp storage(%d) is unlocked\n", storage_id);
				return SDP_STATE_UNLOCKED;
			} else {
				SDP_LOGD("sdp storage(%d) is locked\n", storage_id);
				return SDP_STATE_LOCKED;
			}
		}
	}
	SDP_LOGD("sdp storage(%d) is not in the list\n", storage_id);
	return -1;
}

struct sdp_storage * sdp_get_storage(int storage_id)
{
	struct sdp_storage *tmp_storage;
	list_for_each_entry(tmp_storage, &current_sdp_user->storage_list, list) {
		if (tmp_storage->storage_id == storage_id) {
			SDP_LOGD("storage %d is found\n", storage_id);
			return tmp_storage;
		}
	}
	SDP_LOGD("storage %d is not found\n", storage_id);
	return NULL;
}

static void sdp_print_structure(void)
{
	struct sdp_user *tmp_user;
	struct sdp_storage *tmp_storage;
	SDP_LOGE("=============== Print list start =============\n");
	SDP_LOGE("Current User ID : %d\n", current_sdp_user->user_id);
	list_for_each_entry(tmp_user, &sdp_user_list, list) {
		SDP_LOGE("************ User strat ************\n");
		SDP_LOGE("USER id : %d\n", tmp_user->user_id);
		list_for_each_entry(tmp_storage, &tmp_user->storage_list, list) {
			SDP_LOGE("~~~~~~ Storage start ~~~~~~\n");
			SDP_LOGE("Storage id : %d\n", tmp_storage->storage_id);
			SDP_LOGE("Lock state : %s\n", (tmp_storage->lock_state == SDP_STATE_LOCKED) ? "Locked" : "Unlocked");
			if (tmp_storage->sdpk) {
				SDP_LOGE("SDPK : len(%d)\n", tmp_storage->sdpk->len);
				ecryptfs_dump_hex(tmp_storage->sdpk->data, tmp_storage->sdpk->len);
			}
			if (tmp_storage->protected_key) {
				SDP_LOGE("Protected Key : len(%d)\n", tmp_storage->protected_key->len);
				ecryptfs_dump_hex(tmp_storage->protected_key->data, tmp_storage->protected_key->len);
			}
			SDP_LOGE("~~~~~~ Storage end ~~~~~~\n");
		}
		SDP_LOGE("************ User end ************\n");
	}
	SDP_LOGE("=============== Print list end =============\n");
}

int sdp_write_enc_packet(char **packet, struct ecryptfs_crypt_stat *crypt_stat,
		size_t *written)
{
	size_t i = 0;
	size_t data_len;
	char *message;
	int rc = 0;

	/*
	 *		***** SDP DEC Packet Format *****
	 *	  | Content Type			| 1 byte		|
	 *	  | storage id 			   	| 4 byte		|
	 *	  | sdp key type			| 1 byte		|
	 *	  | File Encryption Key Size 	| 1 byte		|
	 *	  | File Encryption Key	   	| arbitrary	|
	 */
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	data_len = (1 + 4 + 1 + 1 + crypt_stat->key_size);
	*packet = kmalloc(data_len, GFP_KERNEL);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	message = *packet;
	if (!message) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	message[i++] = SDP_CMD_ENC_FEK;
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

	put_unaligned_be32(crypt_stat->storage_id, &message[i]);
	ecryptfs_printk(KERN_DEBUG, ":%d:storage_id=%d\n", __LINE__, crypt_stat->storage_id);
	i += 4;
	message[i++] = SDP_KEY_TYPE_ECDH;
	message[i++] = crypt_stat->key_size;
	memcpy(&message[i], crypt_stat->key, crypt_stat->key_size);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	ecryptfs_dump_hex(crypt_stat->key, crypt_stat->key_size);
	//schedule_timeout_interruptible(1 * HZ);
	i += crypt_stat->key_size;

	*written = i;
out:
	return rc;

}

int sdp_write_dec_packet(struct ecryptfs_crypt_stat *crypt_stat,
	struct ecryptfs_session_key *session_key, char **packet, size_t *packet_len)
{
	size_t i = 0;
	size_t data_len;
	char *message;
	size_t pubkey_len_size;
	int rc;

	/*
	 *		***** SDP DEC Packet Format *****
	 *	  | Content Type					|1 byte		|
	 *	  | storage id						|4 byte		|
	 *	  | sdp key type					| 1 byte		|
	 *	  | Encrypted File Encryption Key Size	|1 byte		|
	 *	  | Encrypted File Encryption Key		|arbitrary	|
	 *	  | Public Key Size in PEM			|1 or 2 bytes	|
	 *	  | Public Key in PEM				|arbitrary	|
	 */
	data_len = (8 + session_key->encrypted_key_size + crypt_stat->pubkey_len);
	*packet = kmalloc(data_len, GFP_KERNEL);
	message = *packet;
	if (!message) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	message[i++] = SDP_CMD_DEC_EFEK;
	put_unaligned_be32(crypt_stat->storage_id, &message[i]);
	i += 4;
	message[i++] = SDP_KEY_TYPE_ECDH;
	message[i++] = session_key->encrypted_key_size;
	memcpy(&message[i], session_key->encrypted_key,
	       session_key->encrypted_key_size);
	i += session_key->encrypted_key_size;
	if (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE) {
		rc = ecryptfs_write_packet_length(&message[i],
							  crypt_stat->pubkey_len,
							  &pubkey_len_size);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error generating FEK enc packet "
					"header; cannot generate pubkey length\n");
			goto out;
		}
		i += pubkey_len_size;
		memcpy(&message[i], crypt_stat->pubkey,
			   crypt_stat->pubkey_len);
		i += crypt_stat->pubkey_len;
	}

	*packet_len = i;
out:
	return rc;

}
static int
sdp_parse_enc_fek_packet(struct ecryptfs_key_record *key_rec,
			struct ecryptfs_crypt_stat *crypt_stat,
		    struct ecryptfs_message *msg)
{
	size_t i = 0;
	char *data;
	size_t data_len;
	size_t message_len;
	int rc;
	int storage_id;
	__be32 sid_nbo;

	/*
	 *              ***** SDP_CMD_RES_EFEK Packet Format *****
	 *    | Content Type                       | 1 byte       |
	 *    | Status Indicator                   | 1 byte       |
	 *    | Storage ID                               | 4 bytes  |
	 *    | Encrypted File Encryption Key Size | 1 or 2 bytes |
	 *    | Encrypted File Encryption Key      | arbitrary    |
	 *    | File Public Key Size                     | 1 or 2 bytes |
	 *    | File Public Key                            | arbitrary    |
	 */
	message_len = msg->data_len;
	data = msg->data;
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	/* verify that everything through the encrypted FEK size is present */
	if (message_len < 6) {
		rc = -EIO;
		printk(KERN_ERR "%s: message_len is [%zd]; minimum acceptable "
		       "message length is [%d]\n", __func__, message_len, 8);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	if (data[i++] != SDP_CMD_RES_EFEK) {
		rc = -EIO;
		printk(KERN_ERR "%s: Type should be SDP_CMD_RES_EFEK\n",
		       __func__);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	if (data[i++]) {
		rc = -EIO;
		printk(KERN_ERR "%s: Status indicator has non zero "
		       "value [%d]\n", __func__, data[i-1]);

		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

	memcpy(&sid_nbo, &data[i], 4);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	storage_id = be32_to_cpu(sid_nbo);
	ecryptfs_printk(KERN_DEBUG, ":%d:user_id = %d\n", __LINE__, storage_id);
	i += 4;
	if (crypt_stat->storage_id != storage_id) {
		SDP_LOGE("user id does not match\n%d!=%d", crypt_stat->storage_id, storage_id);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	key_rec->enc_key_size = data[i++];
	if (message_len < (i + key_rec->enc_key_size)) {
		rc = -EIO;
		printk(KERN_ERR "%s: message_len [%zd]; max len is [%zd]\n",
		       __func__, message_len, (i + key_rec->enc_key_size));
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

	memcpy(key_rec->enc_key, &data[i], key_rec->enc_key_size);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	i += key_rec->enc_key_size;

	rc = ecryptfs_parse_packet_length(&data[i], &crypt_stat->pubkey_len, &data_len);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing pubkey packet length; "
				"rc = [%d]\n", rc);
		goto out;
	}
	if (key_rec->enc_key_size > ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES) {
		rc = -EIO;
		printk(KERN_ERR "%s: Encrypted key_size [%zd] larger than "
		       "the maximum key size [%d]\n", __func__,
		       key_rec->enc_key_size,
		       ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	i += data_len;

	if (message_len < (i + crypt_stat->pubkey_len)) {
		rc = -EIO;
		printk(KERN_ERR "%s: message_len [%zd]; max len is [%zd]\n",
		       __func__, message_len, (i + key_rec->enc_key_size));
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	if (crypt_stat->pubkey_len > ECRYPTFS_SDP_PUBKEY_LEN_MAX) {
		rc = -EIO;
		printk(KERN_ERR "%s: Encrypted key_size [%zd] larger than "
		       "the maximum key size [%d]\n", __func__,
		       key_rec->enc_key_size,
		       ECRYPTFS_SDP_PUBKEY_LEN_MAX);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	memcpy(crypt_stat->pubkey, &data[i], crypt_stat->pubkey_len);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	i += crypt_stat->pubkey_len;
out:
	return rc;
}
static int
sdp_parse_dec_efek_packet(struct ecryptfs_session_key *session_key, struct ecryptfs_crypt_stat *crypt_stat,
		    struct ecryptfs_message *msg)
{
	size_t i = 0;
	char *data;
	size_t data_len;
	size_t m_size;
	size_t message_len;
	int rc;
	int storage_id;
	__be32 sid_nbo;

	/*
	 *              ***** DEC EFEK Packet Format *****
	 *         | Content Type                    | 1 byte       |
	 *         | Status Indicator                 | 1 byte       |
	 *         | Storage ID                            | 4 bytes  |
	 *         | File Encryption Key Size     | 1 or 2 bytes |
	 *         | File Encryption Key            | arbitrary    |
	 */
	message_len = msg->data_len;
	data = msg->data;
	if (message_len < 6) {
		rc = -EIO;
		goto out;
	}
	if (data[i++] != SDP_CMD_RES_FEK) {
		ecryptfs_printk(KERN_ERR, "Type should be SDP_CMD_RES_FEK\n");
		rc = -EIO;
		goto out;
	}
	if (data[i++]) {
		ecryptfs_printk(KERN_ERR, "Status indicator has non-zero value "
				"[%d]\n", data[i-1]);
		rc = -EIO;
		goto out;
	}

	memcpy(&sid_nbo, &data[i], 4);
	storage_id = be32_to_cpu(sid_nbo);
	i += 4;
	if (crypt_stat->storage_id != storage_id) {
		SDP_LOGE("user id does not match\n%d!=%d", crypt_stat->storage_id, storage_id);
		goto out;
	}
	rc = ecryptfs_parse_packet_length(&data[i], &m_size, &data_len);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing packet length; "
				"rc = [%d]\n", rc);
		goto out;
	}
	i += data_len;
	if (message_len < (i + m_size)) {
		ecryptfs_printk(KERN_ERR, "The message received from ecryptfsd "
				"is shorter than expected\n");
		rc = -EIO;
		goto out;
	}
	if (m_size < 3) {
		ecryptfs_printk(KERN_ERR,
				"The decrypted key is not long enough to "
				"include a cipher code and checksum\n");
		rc = -EIO;
		goto out;
	}
	/* The decrypted key includes 1 byte cipher code and 2 byte checksum */
	session_key->decrypted_key_size = m_size;
	if (session_key->decrypted_key_size > ECRYPTFS_MAX_KEY_BYTES) {
		ecryptfs_printk(KERN_ERR, "key_size [%d] larger than "
				"the maximum key size [%d]\n",
				session_key->decrypted_key_size,
				ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES);
		rc = -EIO;
		goto out;
	}
	memcpy(session_key->decrypted_key, &data[i],
	       session_key->decrypted_key_size);
	i += session_key->decrypted_key_size;
out:
	return rc;
}

static int
sdp_encrypt_session_key(struct ecryptfs_crypt_stat *crypt_stat,
			struct ecryptfs_key_record *key_rec)
{
	struct ecryptfs_msg_ctx *msg_ctx = NULL;
	char *payload = NULL;
	size_t payload_len = 0;
	struct ecryptfs_message *msg = NULL;
	struct sdp_user *cur_user;
	struct sdp_storage *tmp_storage;
	struct sdp_key *sym_key;
	int rc = 0;
	ecryptfs_printk(KERN_DEBUG, ":%d:storageid:%d\n", __LINE__, crypt_stat->storage_id);
	cur_user = sdp_get_current_user();
	rc = sdp_is_storage_locked(cur_user, crypt_stat->storage_id);
	if (rc < 0) {
		SDP_LOGE("Invalid storage\n");
		goto out;
	} else if (rc == SDP_STATE_UNLOCKED) { // Unlocked state
		//TODO:: Symmetric key encryption
		list_for_each_entry(tmp_storage, &cur_user->storage_list, list) {
			if (tmp_storage->storage_id == crypt_stat->storage_id) {
				sym_key = tmp_storage->sdpk;
			}
		}
		rc = sdp_aes_crypto(sym_key, crypt_stat->key, key_rec->enc_key, crypt_stat->key_size, SDP_AES_ENC);
		crypt_stat->pubkey_len = 0;
		key_rec->enc_key_size = crypt_stat->key_size;
		goto out;
	}
	rc = sdp_write_enc_packet(&payload, crypt_stat,
		&payload_len);
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating ENC_FEK packet\n");
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	rc = ecryptfs_send_message(payload, payload_len, &msg_ctx);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error sending message to "
				"ecryptfsd: %d\n", rc);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	rc = ecryptfs_wait_for_response(msg_ctx, &msg);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to receive Encrypted FEK Packet "
				"from the user space daemon\n");
		rc = -EIO;
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	rc = sdp_parse_enc_fek_packet(key_rec, crypt_stat, msg);
	if (rc)
		ecryptfs_printk(KERN_ERR, "Error parsing Encrypted FEK packet\n");
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

out:
	if (msg) kfree(msg);
	if (payload) kfree(payload);
	return rc;
}

int
sdp_decrypt_session_key(struct ecryptfs_auth_tok *auth_tok,
				  struct ecryptfs_crypt_stat *crypt_stat)
{
//	u8 cipher_code = 0;
	struct ecryptfs_msg_ctx *msg_ctx;
	struct ecryptfs_message *msg = NULL;
	char *auth_tok_sig;
	char *payload = NULL;
	size_t payload_len = 0;
	struct sdp_storage *tmp_storage;
	struct sdp_key sym_key;
	int rc;
	ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
	if (!(crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE)) {
		//TODO :Decrypt session key by using Protected Key
		if (crypt_stat->storage_id >= 0) {
			struct sdp_storage * store = sdp_get_storage(crypt_stat->storage_id);
			auth_tok->session_key.decrypted_key_size =
				auth_tok->session_key.encrypted_key_size;
			SDP_LOGD("%s:%d:Protected key len = %d\n", __func__, __LINE__, store->protected_key->len);
			rc = sdp_aes_crypto(store->protected_key, auth_tok->session_key.encrypted_key,
				auth_tok->session_key.decrypted_key, auth_tok->session_key.decrypted_key_size, SDP_AES_DEC);
			SDP_LOGD("%s:%d:Decrypt Session key by protected key rc = %d\n", __func__, __LINE__, rc);
			crypt_stat->pubkey_len = 0;
		} else { //use mount wide symmetric key when sdp file is just created.
			sym_key.len = auth_tok->token.private_key.data_len;
			SDP_LOGD("%s:%d:mount wide key len = %d\n", __func__, __LINE__, sym_key.len);
			memcpy(sym_key.data, auth_tok->token.private_key.data, sym_key.len);
			auth_tok->session_key.decrypted_key_size =
				auth_tok->session_key.encrypted_key_size;
			rc = sdp_aes_crypto(&sym_key, auth_tok->session_key.encrypted_key,
								auth_tok->session_key.decrypted_key,
								auth_tok->session_key.decrypted_key_size, SDP_AES_DEC);
			SDP_LOGD("%s:%d:Decrypt Session key by mount wide key rc = %d\n", __func__, __LINE__, rc);
			crypt_stat->pubkey_len = 0;
			zero_out(sym_key.data, sym_key.len);
		}
		goto session_key_decrypted;
	}
	rc = sdp_is_storage_locked(current_sdp_user, crypt_stat->storage_id);
	if (rc < 0) {
		SDP_LOGE("Invalid storage\n");
		return rc;
	} else if (rc == SDP_STATE_UNLOCKED && crypt_stat->pubkey_len == 0) { // Unlocked state
		//TODO:: Symmetric key encryption
		SDP_LOGD("%s:Try Symmetric FEKEK\n", __func__);
		list_for_each_entry(tmp_storage, &current_sdp_user->storage_list, list) {
			if (tmp_storage->storage_id == crypt_stat->storage_id) {
				sym_key.len = tmp_storage->sdpk->len;
				sym_key.type = tmp_storage->sdpk->type;
				memcpy(sym_key.data, tmp_storage->sdpk->data, sym_key.len);
			}
		}
		auth_tok->session_key.decrypted_key_size =
			auth_tok->session_key.encrypted_key_size;
		rc = sdp_aes_crypto(&sym_key, auth_tok->session_key.encrypted_key,
							auth_tok->session_key.decrypted_key,
							auth_tok->session_key.decrypted_key_size, SDP_AES_DEC);
		ecryptfs_printk(KERN_DEBUG, "Decrypted session key:\n");
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
		zero_out(sym_key.data, sym_key.len);
		goto session_key_decrypted;
	} else if (rc == SDP_STATE_LOCKED ) {
		SDP_LOGE("SDP Storage(%d) is locked, Cannot decrypt sdp file\n", crypt_stat->storage_id);
		rc = -EIO;
		goto out;
	}
	rc = ecryptfs_get_auth_tok_sig(&auth_tok_sig, auth_tok);
	if (rc) {
		printk(KERN_ERR "Unrecognized auth tok type: [%d]\n",
		       auth_tok->token_type);
		goto out;
	}
	rc = sdp_write_dec_packet(crypt_stat, &(auth_tok->session_key),
				&payload, &payload_len);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to write sdp dec fek packet\n");
		goto out;
	}
#if SDP_DEBUG
	ecryptfs_printk(KERN_DEBUG, "sdp dec fek payload(len=%zu)\n", payload_len);

	ecryptfs_dump_hex(payload, payload_len);
#endif
	rc = ecryptfs_send_message(payload, payload_len, &msg_ctx);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error sending message to "
				"ecryptfsd: %d\n", rc);
		goto out;
	}
	rc = ecryptfs_wait_for_response(msg_ctx, &msg);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to receive SDP DEC EFEK packet "
				"from the user space daemon\n");
		rc = -EIO;
		goto out;
	}
	rc = sdp_parse_dec_efek_packet(&(auth_tok->session_key), crypt_stat, msg);
	if (rc) {
		printk(KERN_ERR "Failed to parse DEC EFEK packet; rc = [%d]\n",
		       rc);
		goto out;
	}
session_key_decrypted:
	auth_tok->session_key.flags |= ECRYPTFS_CONTAINS_DECRYPTED_KEY;
	memcpy(crypt_stat->key, auth_tok->session_key.decrypted_key,
	       auth_tok->session_key.decrypted_key_size);
	crypt_stat->key_size = auth_tok->session_key.decrypted_key_size;

	memset(auth_tok->session_key.decrypted_key, 0, auth_tok->session_key.decrypted_key_size);
	/*
	rc = ecryptfs_cipher_code_to_string(crypt_stat->cipher, cipher_code);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Cipher code [%d] is invalid\n",
				cipher_code)
		goto out;
	}*/
	strlcpy(crypt_stat->cipher, "aes", 4);
	crypt_stat->flags |= ECRYPTFS_KEY_VALID;
	if (ecryptfs_verbosity > 0) {
		ecryptfs_printk(KERN_DEBUG, "Decrypted session key:\n");
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
	}
	ecryptfs_printk(KERN_DEBUG, "%s::%d\n", __func__, __LINE__);
out:
	if (msg) kfree(msg);
	if (payload) kfree(payload);
	return rc;
}


int sdp_write_sdp_header(char *dest, size_t *remaining_bytes,
		   struct key *auth_tok_key, struct ecryptfs_auth_tok *auth_tok,
		   struct ecryptfs_crypt_stat *crypt_stat,
		   struct ecryptfs_key_record *key_rec, size_t *packet_size)
{
	size_t i;
	size_t encrypted_session_key_valid = 0;
	size_t packet_size_length;
	size_t max_packet_size;
	int rc = 0;

	ecryptfs_printk(KERN_DEBUG, ":%d\n",  __LINE__);
	(*packet_size) = 0;
	ecryptfs_from_hex(key_rec->sig, auth_tok->token.private_key.signature,
			  ECRYPTFS_SIG_SIZE);
	encrypted_session_key_valid = 0;
	ecryptfs_printk(KERN_DEBUG, ":%d:crypt_stat key size=%zu\n",  __LINE__, crypt_stat->key_size);
	for (i = 0; i < crypt_stat->key_size; i++)
		encrypted_session_key_valid |=
			auth_tok->session_key.encrypted_key[i];
	ecryptfs_printk(KERN_DEBUG, ":%d:key_valid=%zu\n", __LINE__, encrypted_session_key_valid);
	ecryptfs_printk(KERN_DEBUG, ":%d:key_size=%zu\n",	__LINE__, crypt_stat->key_size);
	if (encrypted_session_key_valid) {
		ecryptfs_printk(KERN_DEBUG, ":%d:session key valid\n", __LINE__);
		memcpy(key_rec->enc_key,
		       auth_tok->session_key.encrypted_key,
		       auth_tok->session_key.encrypted_key_size);
		up_write(&(auth_tok_key->sem));
		key_put(auth_tok_key);
		goto encrypted_session_key_set;
	}
	ecryptfs_printk(KERN_DEBUG, ":%d\n",  __LINE__);
	ecryptfs_printk(KERN_DEBUG, ":%d,token.private_key.key_size = %d\n", __LINE__, auth_tok->token.private_key.key_size);
	if (auth_tok->session_key.encrypted_key_size == 0)
		auth_tok->session_key.encrypted_key_size =
			auth_tok->token.private_key.key_size;

	ecryptfs_printk(KERN_DEBUG, ":%d::encrypted session key size=%d\n",  __LINE__, auth_tok->session_key.encrypted_key_size);
	ecryptfs_printk(KERN_DEBUG, ":%d::Encrypt session key start(SDP)\n",  __LINE__);
	if (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE) {
		rc = sdp_encrypt_session_key(crypt_stat, key_rec);
	} else {
		//TODO:: REK Protected symmetric key encryption loaded from the user
		if (crypt_stat->storage_id >= 0) {
			struct sdp_storage * store = sdp_get_storage(crypt_stat->storage_id);
			rc = sdp_aes_crypto(store->protected_key, crypt_stat->key, key_rec->enc_key, crypt_stat->key_size, SDP_AES_ENC);
			key_rec->enc_key_size = crypt_stat->key_size;
		} else { //use default key when sdp file is just created.
			struct sdp_key default_key;
			default_key.len = auth_tok->token.private_key.key_size;
			default_key.type = 0;
			memcpy(default_key.data, auth_tok->token.private_key.data, default_key.len);
			rc = sdp_aes_crypto(&default_key, crypt_stat->key, key_rec->enc_key, crypt_stat->key_size, SDP_AES_ENC);
			key_rec->enc_key_size = crypt_stat->key_size;
			zero_out(default_key.data, default_key.len);
		}
	}
	up_write(&(auth_tok_key->sem));
	key_put(auth_tok_key);
	ecryptfs_printk(KERN_DEBUG, ":%d::Encrypt session key end(SDP)\n",  __LINE__);
	//schedule_timeout_interruptible(1 * HZ);
	if (rc) {
		printk(KERN_ERR "Failed to encrypt session key via a key "
		       "module; rc = [%d]\n", rc);
		goto out;
	}
	if (ecryptfs_verbosity > 0) {
		ecryptfs_printk(KERN_DEBUG, "Encrypted key:\n");
		ecryptfs_dump_hex(key_rec->enc_key, key_rec->enc_key_size);
	}
encrypted_session_key_set:
	max_packet_size = (1					/* Packet type */
			   + 3							/* Max packet size */
			   + 1							/* Version */
			   + ECRYPTFS_SIG_SIZE			/* Key identifier */
			   + 1							/* Cipher identifier */
			   + 4							/* storage id*/
			   + 2							/* File public key size length */
			   + crypt_stat->pubkey_len		/* File public key size*/
			   + key_rec->enc_key_size);	/* Encrypted key size */

	if (max_packet_size > (*remaining_bytes)) {
		printk(KERN_ERR "Packet length larger than maximum allowable; "
		       "need up to [%td] bytes, but there are only [%td] "
		       "available\n", max_packet_size, (*remaining_bytes));
		rc = -EINVAL;
		goto out;
	}
	//Packet Type;
	dest[(*packet_size)++] = ECRYPTFS_SDP_PACKET_TYPE;
	//Max packet size
	rc = ecryptfs_write_packet_length(&dest[(*packet_size)],
					  (max_packet_size - 5),
					  &packet_size_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating SDP packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	(*packet_size) += packet_size_length;
	//Version
	dest[(*packet_size)++] = 0x04; /* version 4 */
	//Signature
	memcpy(&dest[(*packet_size)], key_rec->sig, ECRYPTFS_SIG_SIZE);
	(*packet_size) += ECRYPTFS_SIG_SIZE;
	//CIPHER Code
	if (crypt_stat->pubkey_len != 0) {
		dest[(*packet_size)++] = CIPHER_ECDH;
	} else {
		dest[(*packet_size)++] = RFC2440_CIPHER_AES_256;
	}
	// storage id
	put_unaligned_be32(crypt_stat->storage_id, &dest[(*packet_size)]);
	(*packet_size) += 4;
	rc = ecryptfs_write_packet_length(&dest[(*packet_size)],
					  crypt_stat->pubkey_len,
					  &packet_size_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating SDP packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	//(*packet_size) += packet_size_length;
	(*packet_size) += 2;
	SDP_LOGD("%s:%d:pubkeylen:%zu\n", __func__, __LINE__, crypt_stat->pubkey_len);
	if (crypt_stat->pubkey_len > 0) {
		memcpy(&dest[(*packet_size)], crypt_stat->pubkey,
		       crypt_stat->pubkey_len);
		(*packet_size) += crypt_stat->pubkey_len;
	}

	// Encrypted FEK
	memcpy(&dest[(*packet_size)], key_rec->enc_key,
	       key_rec->enc_key_size);
	(*packet_size) += key_rec->enc_key_size;
	SDP_LOGD("%s:%d:Encrypted session key len:%zu\n", __func__, __LINE__, key_rec->enc_key_size);
	ecryptfs_printk(KERN_DEBUG, ":%d", __LINE__);
	ecryptfs_dump_hex(&dest[0],*packet_size);

out:
	if (rc)
		(*packet_size) = 0;
	else
		(*remaining_bytes) -= (*packet_size);
	return rc;
}

int
sdp_parse_sdp_header(struct ecryptfs_crypt_stat *crypt_stat,
		   unsigned char *data, struct list_head *auth_tok_list,
		   struct ecryptfs_auth_tok **new_auth_tok,
		   size_t *packet_size, size_t max_packet_size)
{
	size_t body_size;
	struct ecryptfs_auth_tok_list_item *auth_tok_list_item;
	size_t length_size;
	int rc = 0;
	ecryptfs_printk(KERN_DEBUG, "%d", __LINE__);
	(*packet_size) = 0;
	(*new_auth_tok) = NULL;
	/**
	 * This format is inspired by OpenPGP; see RFC 2440
	 * packet tag 1
	 *
	 * Tag 1 identifier (1 byte)
	 * Max Tag 1 packet size (max 3 bytes)
	 * Version (1 byte)
	 * Key identifier (8 bytes; ECRYPTFS_SIG_SIZE)
	 * Cipher identifier (1 byte)
	 * storage id	(4 bytes)
	 * File public key size (1 or 2 bytes)
	 * File public key (arbitrary)
	 * Encrypted key size (arbitrary)
	 * 17 bytes minimum packet size
	 */
	if (unlikely(max_packet_size < 17)) {
		printk(KERN_ERR "Invalid max packet size; must be >=12\n");
		rc = -EINVAL;
		goto out;
	}
	if (data[(*packet_size)++] != ECRYPTFS_SDP_PACKET_TYPE) {
		printk(KERN_ERR "Enter w/ first byte != 0x%.2x\n",
		       ECRYPTFS_SDP_PACKET_TYPE);
		rc = -EINVAL;
		goto out;
	}
	/* Released: wipe_auth_tok_list called in ecryptfs_parse_packet_set or
	 * at end of function upon failure */
	auth_tok_list_item =
		kmem_cache_zalloc(ecryptfs_auth_tok_list_item_cache,
				  GFP_KERNEL);
	if (!auth_tok_list_item) {
		printk(KERN_ERR "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	(*new_auth_tok) = &auth_tok_list_item->auth_tok;
	rc = ecryptfs_parse_packet_length(&data[(*packet_size)], &body_size,
					  &length_size);
	if (rc) {
		printk(KERN_WARNING "Error parsing packet length; "
		       "rc = [%d]\n", rc);
		goto out_free;
	}
	if (unlikely(body_size < (ECRYPTFS_SIG_SIZE + 2))) {
		printk(KERN_WARNING "Invalid body size ([%td])\n", body_size);
		rc = -EINVAL;
		goto out_free;
	}
	(*packet_size) += length_size;
	if (unlikely((*packet_size) + body_size > max_packet_size)) {
		printk(KERN_WARNING "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out_free;
	}
	if (unlikely(data[(*packet_size)++] != 0x04)) {
		printk(KERN_WARNING "Unknown version number [%d]\n",
		       data[(*packet_size) - 1]);
		rc = -EINVAL;
		goto out_free;
	}
	ecryptfs_to_hex((*new_auth_tok)->token.private_key.signature,
			&data[(*packet_size)], ECRYPTFS_SIG_SIZE);
	*packet_size += ECRYPTFS_SIG_SIZE;
	/* This byte is skipped because the kernel does not need to
	 * know which public key encryption algorithm was used */
	(*packet_size)++;
	ecryptfs_printk(KERN_DEBUG, "%d\n", __LINE__);
	crypt_stat->storage_id = get_unaligned_be32(data + *packet_size);
	ecryptfs_printk(KERN_DEBUG, "%d: storage_id=%d\n", __LINE__, crypt_stat->storage_id);
	(*packet_size) += 4;
	rc = ecryptfs_parse_packet_length(&data[(*packet_size)], &crypt_stat->pubkey_len,
					  &length_size);
	if (rc) {
		printk(KERN_WARNING "Error parsing packet length; "
		       "rc = [%d]\n", rc);
		goto out_free;
	}
	//(*packet_size) += length_size;
	(*packet_size) += 2;
	if (crypt_stat->pubkey_len > 0) {
		memcpy(crypt_stat->pubkey,
		       &data[(*packet_size)], crypt_stat->pubkey_len);
		SDP_LOGE("%s::%d:pubkey len = %zu\n", __FUNCTION__, __LINE__,
					crypt_stat->pubkey_len);
		ecryptfs_dump_hex(&data[(*packet_size)], crypt_stat->pubkey_len);
		(*packet_size) += crypt_stat->pubkey_len;
	}
	(*new_auth_tok)->session_key.encrypted_key_size =
		(body_size - (ECRYPTFS_SIG_SIZE + 1 + 4 + 2 + crypt_stat->pubkey_len));
	SDP_LOGE("%s::%d:sessionkey.encrypted_key_size = %d\n", __FUNCTION__, __LINE__,
				(*new_auth_tok)->session_key.encrypted_key_size);

	if ((*new_auth_tok)->session_key.encrypted_key_size
	    > ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES) {
		printk(KERN_WARNING "Tag 1 packet contains key larger "
		       "than ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES");
		rc = -EINVAL;
		goto out;
	}
	memcpy((*new_auth_tok)->session_key.encrypted_key,
	       &data[(*packet_size)], (*new_auth_tok)->session_key.encrypted_key_size);
	ecryptfs_dump_hex((*new_auth_tok)->session_key.encrypted_key, (*new_auth_tok)->session_key.encrypted_key_size);
	(*packet_size) += (*new_auth_tok)->session_key.encrypted_key_size;
	(*new_auth_tok)->session_key.flags &=
		~ECRYPTFS_CONTAINS_DECRYPTED_KEY;
	(*new_auth_tok)->session_key.flags |=
		ECRYPTFS_CONTAINS_ENCRYPTED_KEY;
	(*new_auth_tok)->token_type = ECRYPTFS_PRIVATE_KEY;
	(*new_auth_tok)->flags = 0;
	(*new_auth_tok)->session_key.flags &=
		~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_DECRYPT);
	(*new_auth_tok)->session_key.flags &=
		~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_ENCRYPT);
	list_add(&auth_tok_list_item->list, auth_tok_list);
	goto out;
out_free:
	(*new_auth_tok) = NULL;
	memset(auth_tok_list_item, 0,
	       sizeof(struct ecryptfs_auth_tok_list_item));
	kmem_cache_free(ecryptfs_auth_tok_list_item_cache,
			auth_tok_list_item);
out:
	if (rc)
		(*packet_size) = 0;
	SDP_LOGD("%d: SDP Header Read:\n", __LINE__);
	ecryptfs_dump_hex(data, *packet_size);
	return rc;
}

int sdp_aes_crypto(struct sdp_key *key, char *source, char *dest, int len, int operation) {
	int rc;
	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;
	struct scatterlist src_sg, dst_sg;
	u8 iv[16];

	if (key == NULL) {
		return -EINVAL;
	}
	tfm = crypto_alloc_blkcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
	if (!IS_ERR(tfm)) {
		crypto_blkcipher_setkey(tfm, key->data, key->len);
	} else {
		SDP_LOGE("Failed to allocate blkcipher\n");
	}
	if (tfm == NULL) {
		return -ENOMEM;
	}
	memset(&iv, 0, sizeof(iv));
	desc.tfm = tfm;
	desc.info = iv;
	desc.flags = 0;

	sg_init_one(&src_sg, source, len);
	sg_init_one(&dst_sg, dest, len);

	if (operation == SDP_AES_ENC) {
		rc = crypto_blkcipher_encrypt_iv(&desc, &dst_sg, &src_sg, len);
	} else {
		rc = crypto_blkcipher_decrypt_iv(&desc, &dst_sg, &src_sg, len);
	}

	if (rc < 0) {
		SDP_LOGE("Failed to %s", (operation == SDP_AES_ENC) ? "encrypt" : "decrypt");
	}
	crypto_free_blkcipher(tfm);

	return rc;
}

static int sdp_set_key(struct ecryptfs_crypt_stat *crypt_stat) {
	int rc = 0;

	mutex_lock(&crypt_stat->cs_tfm_mutex);
	if (!(crypt_stat->flags & ECRYPTFS_KEY_SET)) {
		rc = crypto_ablkcipher_setkey(crypt_stat->tfm, crypt_stat->key,
				crypt_stat->key_size);
		if (rc) {
			ecryptfs_printk(KERN_ERR,
					"Error setting key; rc = [%d]\n",
					rc);
			rc = -EINVAL;
			goto out;
		}
		crypt_stat->flags |= ECRYPTFS_KEY_SET;
		crypt_stat->flags |= ECRYPTFS_KEY_VALID;
	}
out:
	mutex_unlock(&crypt_stat->cs_tfm_mutex);
	return rc;
}

static int sdp_copy_mount_wide_sigs_to_inode_sigs(
	struct ecryptfs_crypt_stat *crypt_stat,
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat)
{
	struct ecryptfs_global_auth_tok *global_auth_tok;
	int rc = 0;

	mutex_lock(&crypt_stat->keysig_list_mutex);
	mutex_lock(&mount_crypt_stat->global_auth_tok_list_mutex);

	list_for_each_entry(global_auth_tok,
				&mount_crypt_stat->global_auth_tok_list,
				mount_crypt_stat_list) {
		if (global_auth_tok->flags & ECRYPTFS_AUTH_TOK_FNEK)
			continue;
		rc = ecryptfs_add_keysig(crypt_stat, global_auth_tok->sig);
		if (rc) {
			SDP_LOGE("%s:Error adding keysig; rc = [%d]\n", __func__, rc);
			goto out;
		}
	}

out:
	mutex_unlock(&mount_crypt_stat->global_auth_tok_list_mutex);
	mutex_unlock(&crypt_stat->keysig_list_mutex);
	return rc;
}

static int sdp_file_update_crypt_flag(struct dentry *dentry, enum sdp_op operation)
{
	int rc = 0;
	struct inode *inode;
	struct inode *lower_inode;
	struct ecryptfs_crypt_stat *crypt_stat;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;
	u32 tmp_flags;

	SDP_LOGE("%s(operation:%s) entered\n", __func__, (operation == TO_SENSITIVE)? "To Sensitive" : "To Protected");

	crypt_stat = &ecryptfs_inode_to_private(dentry->d_inode)->crypt_stat;
	mount_crypt_stat = &ecryptfs_superblock_to_private(dentry->d_sb)->mount_crypt_stat;
	if (!(crypt_stat->flags & ECRYPTFS_STRUCT_INITIALIZED))
		ecryptfs_init_crypt_stat(crypt_stat);
	inode = dentry->d_inode;
	lower_inode = ecryptfs_inode_to_lower(inode);

	/*
	 * To update metadata we need to make sure keysig_list contains fekek.
	 * Because our EFEK is stored along with key for protected file.
	 */

	if(list_empty(&crypt_stat->keysig_list))
		sdp_copy_mount_wide_sigs_to_inode_sigs(crypt_stat, mount_crypt_stat);

	mutex_lock(&crypt_stat->cs_mutex);
	rc = ecryptfs_get_lower_file(dentry, inode);
	if (rc) {
		mutex_unlock(&crypt_stat->cs_mutex);
		SDP_LOGE("ecryptfs_get_lower_file rc=%d\n", rc);
		return rc;
	}

	tmp_flags = crypt_stat->flags;
	if (operation == TO_SENSITIVE) {
		crypt_stat->flags |= ECRYPTFS_SDP_SENSITIVE;
		/*
		* Set sensitive to inode mapping
		*/
		//ecryptfs_set_mapping_sensitive(inode, mount_crypt_stat->userid, TO_SENSITIVE);
	} else {
		crypt_stat->flags &= ~ECRYPTFS_SDP_SENSITIVE;
		/*
		* Set protected to inode mapping
		*/
		//ecryptfs_set_mapping_sensitive(inode, mount_crypt_stat->userid, TO_PROTECTED);
	}

	rc = ecryptfs_write_metadata(dentry, inode);
	if (rc) {
		crypt_stat->flags = tmp_flags;
		SDP_LOGE("ecryptfs_write_metadata rc=%d\n", rc);
		goto out;
	}

	rc = ecryptfs_write_inode_size_to_metadata(inode);
	if (rc) {
		SDP_LOGE("Problem with "
				"ecryptfs_write_inode_size_to_metadata; "
				"rc = [%d]\n", rc);
		goto out;
	}

out:
	mutex_unlock(&crypt_stat->cs_mutex);
	ecryptfs_put_lower_file(inode);
	fsstack_copy_attr_all(inode, lower_inode);
	return rc;
}

int sdp_file_set_sensitive(struct dentry *dentry, int storage_id) {
	int rc = 0;
	struct inode *inode = dentry->d_inode;
	struct ecryptfs_crypt_stat *crypt_stat =
			&ecryptfs_inode_to_private(inode)->crypt_stat;
	struct ecryptfs_key_record *key_rec;

	key_rec = kmem_cache_alloc(ecryptfs_key_record_cache, GFP_KERNEL);
	if (!key_rec) {
		rc = -ENOMEM;
		goto out;
	}
	memset(key_rec, 0, sizeof(*key_rec));
	SDP_LOGD("%s(%s)\n", __func__, dentry->d_name.name);
	crypt_stat->storage_id = storage_id;
	crypt_stat->flags |= ECRYPTFS_SDP_SENSITIVE;
	if (S_ISDIR(inode->i_mode)) {
		crypt_stat->flags |= ECRYPTFS_SDP_SENSITIVE;
		rc = 0;
	} else if(S_ISREG(inode->i_mode)) {
		if (crypt_stat->key_size > ECRYPTFS_MAX_KEY_BYTES) {
			SDP_LOGE("%s Too large key_size\n", __func__);
			rc = -EFAULT;
			goto out;
	    }
#if 0
	    /*
	     * We don't have to clear FEK after set-sensitive.
	     * FEK will be closed when the file is closed
	     */
	    memset(crypt_stat->key, 0, crypt_stat->key_size);
	    crypt_stat->flags &= ~(ECRYPTFS_KEY_SET);
#else
	    /*
	     * set-key after set sensitive file.
	     * Well when the file is just created and we do set_sensitive, the key is not set in the
	     * tfm. later SDP code, set-key is done while encryption, trying to decrypt EFEK.
	     *
	     * Here is the case in locked state user process want to create/write a file.
	     * the process open the file, automatically becomes sensitive by vault logic,
	     * and do the encryption, then boom. failed to decrypt EFEK even if FEK is
	     * available
	     */
		SDP_LOGD("%s:%d:key size = %zu\n", __func__, __LINE__, crypt_stat->key_size);
	    rc = sdp_set_key(crypt_stat);
	    if(rc) goto out;
#endif
		SDP_LOGD("%s:%d:key size = %zu\n", __func__, __LINE__, crypt_stat->key_size);
	    rc = sdp_file_update_crypt_flag(dentry, TO_SENSITIVE);
		if (rc < 0) {
			SDP_LOGE("Failed to update sdp flag");
			crypt_stat->flags &= ~ECRYPTFS_SDP_SENSITIVE;
		}
	}

out:
	return rc;
}
int sdp_file_set_protected(struct dentry *dentry) {
	int rc = 0;
	struct inode *inode = dentry->d_inode;
	struct ecryptfs_crypt_stat *crypt_stat =
			&ecryptfs_inode_to_private(inode)->crypt_stat;

	SDP_LOGD("%s(%s)\n", __func__, dentry->d_name.name);

	if(S_ISDIR(inode->i_mode)) {
		crypt_stat->flags &= ~ECRYPTFS_SDP_SENSITIVE;
		rc = 0;
	} else {
		SDP_LOGE("FEK : \n");
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
		if(!(crypt_stat->flags & ECRYPTFS_KEY_VALID)) {
			SDP_LOGE("get_sdp_dek: key is not set\n");
			rc = -EIO;
			goto out;
		}
		/*
	         * TODO : double check if need to compute iv here
	         */
		rc = ecryptfs_compute_root_iv(crypt_stat);
		if (rc) {
			ecryptfs_printk(KERN_ERR, "Error computing "
					"the root IV\n");
			goto out;
		}

		rc = sdp_set_key(crypt_stat);
		if(rc) goto out;

		rc = sdp_file_update_crypt_flag(dentry, TO_PROTECTED);
		if (rc) {
			SDP_LOGE("Failed to update crypt flags to protected\n");
			goto out;
		}
	}
out:
	return rc;
}

static int sdp_open_ctl(struct inode *inode, struct file *file)
{
	return 0;
}

static int sdp_release_ctl(struct inode *ignored, struct file *file)
{
	return 0;
}
int sdp_file_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *ubuf = (void __user *)arg;
	struct dentry *ecryptfs_dentry = file->f_path.dentry;
	struct inode *inode = ecryptfs_dentry->d_inode;
	struct ecryptfs_crypt_stat *crypt_stat =
			&ecryptfs_inode_to_private(inode)->crypt_stat;

	SDP_LOGD("%s(%s)\n", __func__, ecryptfs_dentry->d_name.name);

	if (!(crypt_stat->flags & ECRYPTFS_SDP_ENABLED)) {
		SDP_LOGE("SDP not enabled, skip sdp ioctl\n");
		return -ENOTTY;
	}
	switch (cmd) {
	case SDP_FILE_IOCTL_GET_INFO: {
		sdp_arg req;

		SDP_LOGD("ECRYPTFS_IOCTL_GET_SDP_INFO\n");

		memset(&req, 0, sizeof(sdp_arg));
		if(copy_from_user(&req, ubuf, sizeof(req))) {
			SDP_LOGE("can't copy from user\n");
			return -EFAULT;
		} else {
			mutex_lock(&crypt_stat->cs_mutex);
			if (crypt_stat->flags & ECRYPTFS_SDP_ENABLED) {
				req.is_enabled = 1;
			} else {
				req.is_enabled = 0;
			}
			if (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE) {
				req.is_sensitive = 1;
			} else {
				req.is_sensitive = 0;
			}
			mutex_unlock(&crypt_stat->cs_mutex);
		}
		if(copy_to_user(ubuf, &req, sizeof(req))) {
			SDP_LOGE("can't copy to user\n");
			return -EFAULT;
		}

		break;
		}
	case SDP_FILE_IOCTL_SET_SENSITIVE: {
		sdp_set_sensitive_params req;
		SDP_LOGD("%s:SDP_FILE_IOCTL_SET_SENSITIVE\n", __func__);
		if (crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE) {
			SDP_LOGE("already sensitive file\n");
			return 0;
		}


		if (S_ISDIR(ecryptfs_dentry->d_inode->i_mode)) {
			SDP_LOGE("Set sensitive(chamber) directory\n");
			crypt_stat->flags |= ECRYPTFS_SDP_SENSITIVE;
			break;
		}

		memset(&req, 0, sizeof(sdp_set_sensitive_params));
		if(copy_from_user(&req, ubuf, sizeof(req))) {
			SDP_LOGE("can't copy from user\n");
			memset(&req, 0, sizeof(sdp_set_sensitive_params));
			return -EFAULT;
		} else {
		//TODO : implement function set protect
			if (sdp_file_set_sensitive(ecryptfs_dentry, req.storage_id)) {
				SDP_LOGE("failed to set sensitive\n");
				memset(&req, 0, sizeof(sdp_set_sensitive_params));
				return -EFAULT;
			}
		}
		memset(&req, 0, sizeof(sdp_arg));
		break;
	}

	case SDP_FILE_IOCTL_SET_PROTECTED: {
		sdp_arg req;

		SDP_LOGD("%s:SDP_FILE_IOCTL_SET_PROTECTED\n", __func__);
		if (!(crypt_stat->flags & ECRYPTFS_SDP_SENSITIVE)) {
			SDP_LOGE("already protected file\n");
			return 0;
		}

		if (S_ISDIR(ecryptfs_dentry->d_inode->i_mode)) {
			SDP_LOGE("Set protected directory\n");
			crypt_stat->flags &= ~ECRYPTFS_SDP_SENSITIVE;
			break;
		}

		memset(&req, 0, sizeof(sdp_arg));
		if(copy_from_user(&req, ubuf, sizeof(req))) {
			SDP_LOGE("can't copy from user\n");
			memset(&req, 0, sizeof(sdp_arg));
			return -EFAULT;
		} else {
		//TODO : implement function set protect
			if (sdp_file_set_protected(ecryptfs_dentry)) {
				SDP_LOGE("failed to set protected\n");
				memset(&req, 0, sizeof(sdp_arg));
				return -EFAULT;
			}
		}
		memset(&req, 0, sizeof(sdp_arg));
		break;
	}

	default: {
		return -EINVAL;
		break;
		}

	}
	return 0;
}

static long sdp_do_ioctl_ctl(unsigned int minor, unsigned int cmd,
		unsigned long arg) {
	long ret = 0;
	void __user *ubuf = (void __user *)arg;
	void *cleanup = NULL;
	unsigned int size;

	switch (cmd) {
	/*
	 * Event when device unlocked.
	 *
	 * Read private key and decrypt with user password.
	 */
	case SDP_ON_DEVICE_UNLOCKED: {
		struct sdp_key *key = kzalloc(sizeof(struct sdp_key), GFP_KERNEL);
		sdp_unlock_params *params = kzalloc(sizeof(sdp_unlock_params), GFP_KERNEL);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_DEVICE_UNLOCKED\n");
		cleanup = params;
		size = sizeof(sdp_unlock_params);

		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from user params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d:received key data length %d\n", __LINE__, params->key.len);
		key->len = params->key.len;
		key->type = params->key.type;
		memcpy(key->data, params->key.data, key->len);
		ecryptfs_dump_hex(key->data, key->len);
		ret = sdp_storage_unlock(current_sdp_user, params->storage_id, key);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		g_locked_state = 0;
		break;
	}
	/*
	 * Event when device is locked.
	 *
	 * Nullify private key which prevents decryption.
	 */
	case SDP_ON_DEVICE_LOCKED: {
		sdp_lock_params *params = kzalloc(sizeof(sdp_lock_params), GFP_KERNEL);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);

		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_DEVICE_LOCKED\n");
		cleanup = params;
		size = sizeof(sdp_lock_params);

		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from user params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		sdp_storage_lock(current_sdp_user, params->storage_id);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		g_locked_state = 1;
		ecryptfs_mm_drop_cache(params->storage_id);
		break;
	}

	case SDP_ON_ADD_USER: {
		sdp_user_ctl_params *params = kzalloc(sizeof(sdp_user_ctl_params), GFP_KERNEL);
		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_ADD_USER\n");
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		cleanup = params;
		size = sizeof(sdp_user_ctl_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from user params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		ret = sdp_user_add(params->user_id);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (ret < 0) {
			SDP_LOGE("Failed to add user in kernel\n");
		} else {
			SDP_LOGD("user %d is added\n", params->user_id);
		}
		break;
	}

	case SDP_ON_DEL_USER: {
		sdp_user_ctl_params *params = kzalloc(sizeof(sdp_user_ctl_params), GFP_KERNEL);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_DEL_USER\n");
		cleanup = params;
		size = sizeof(sdp_user_ctl_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from user params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		ret = sdp_user_del(params->user_id);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (ret < 0) {
			SDP_LOGE("Failed to add user in kernel\n");
		} else {
			SDP_LOGD("user %d is deleted\n", params->user_id);
		}
		break;

	}

	case SDP_ON_CHANGE_USER: {
		sdp_user_ctl_params *params = kzalloc(sizeof(sdp_user_ctl_params), GFP_KERNEL);
		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_CHANGE_USER\n");
		cleanup = params;
		size = sizeof(sdp_user_ctl_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from user params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		ret = sdp_set_curret_user(params->user_id);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (ret < 0) {
			SDP_LOGE("Failed to add user in kernel\n");
		} else {
			SDP_LOGD("user %d is set to current user\n", params->user_id);
		}
		break;
	}

	case SDP_ON_ADD_STORAGE: {
		struct sdp_key *key = kzalloc(sizeof(struct sdp_key), GFP_KERNEL);
		sdp_storage_ctl_params *params = kzalloc(sizeof(sdp_storage_ctl_params), GFP_KERNEL);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_ADD_STORAGE\n");
		cleanup = params;
		size = sizeof(sdp_storage_ctl_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from storage params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_dump_hex((char *)params, size);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		key->len = params->key.len;
		key->type = params->key.type;
		SDP_LOGD("Protected key len : %d\n", params->key.len);
		SDP_LOGD("Protected key type : %d\n", key->type = params->key.type);
		memcpy(key->data, params->key.data, key->len);
		ret = sdp_storage_add(params->user_id, params->storage_id, key);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (ret < 0) {
			SDP_LOGE("Failed to add storage in kernel\n");
		} else {
			SDP_LOGD("storage %d added\n", params->storage_id);
		}
		break;

	}

	case SDP_ON_DEL_STORAGE: {
		sdp_storage_ctl_params *params = kzalloc(sizeof(sdp_storage_ctl_params), GFP_KERNEL);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (params == NULL) {
			ret = -ENOMEM;
			goto err;
		}
		SDP_LOGD("SDP_ON_DEL_STORAGE\n");
		cleanup = params;
		size = sizeof(sdp_storage_ctl_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from storage params\n");
			ret = -EFAULT;
			goto err;
		}
		ret = sdp_storage_del(params->user_id, params->storage_id);
		if (ret < 0) {
			SDP_LOGE("Failed to delete storage in kernel\n");
		} else {
			SDP_LOGD("storage %d deleted\n", params->storage_id);
		}
		break;

	}
	case SDP_ON_GET_STORAGE_INFO: {
		sdp_storage_info_params *params = kzalloc(sizeof(sdp_storage_info_params), GFP_KERNEL);
		struct sdp_storage* storage;
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		SDP_LOGD("SDP_ON_GET_STORAGE_INFO\n");
		cleanup = params;
		size = sizeof(sdp_storage_info_params);
		if(copy_from_user(params, ubuf, size)) {
			SDP_LOGE("can't copy from storage params\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		storage = sdp_get_storage(params->storage_id);
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		if (!storage) {
			SDP_LOGE("Failed to get storage info\n");
			goto err;
		} else {
			SDP_LOGD("storage %d information\n", params->storage_id);
		}
		params->lock_state = storage->lock_state;

		if (copy_to_user(ubuf, params, size)) {
			SDP_LOGE("can't copy storage params to user\n");
			ret = -EFAULT;
			goto err;
		}
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		sdp_print_structure();
		ecryptfs_printk(KERN_DEBUG, ":%d\n", __LINE__);
		break;

	}
	default:
		SDP_LOGE("%s case default\n", __func__);
		ret = -EINVAL;
		break;
	}

err:
	if (cleanup) {
		zero_out((char *)cleanup, size);
		kfree(cleanup);
	}
	return ret;
}

static int is_root(void) {
	return (current_uid() == 0) ? 1 : 0;
}

static int is_system(void) {
	return (current_uid() == 1000) ? 1 : 0;
}

static long sdp_ioctl_ctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	unsigned int minor;
	if(!is_system() && !is_root()) {
		SDP_LOGE("Current process can't access evt device\n");
		SDP_LOGE("Current process info :: "
				"uid=%u gid=%u euid=%u egid=%u suid=%u sgid=%u "
				"fsuid=%u fsgid=%u\n",
				current_uid(), current_gid(), current_euid(),
				current_egid(), current_suid(), current_sgid(),
				current_fsuid(), current_fsgid());
		SDP_LOGD("Access denied to evt device");
		return -EACCES;
	}

	minor = iminor(file->f_path.dentry->d_inode);
	return sdp_do_ioctl_ctl(minor, cmd, arg);
}


const struct file_operations sdp_fops_ctl = {
		.owner = THIS_MODULE,
		.open = sdp_open_ctl,
		.release = sdp_release_ctl,
		.unlocked_ioctl = sdp_ioctl_ctl,
		.compat_ioctl = sdp_ioctl_ctl,
};

static struct miscdevice sdp_misc_ctl = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "sdp_ctl",
		.fops = &sdp_fops_ctl,
};
static ssize_t sdp_show_locked_state(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct sdp_storage *tmp_storage;
	tmp_storage = sdp_get_storage(current_sdp_user->user_id);
	if (tmp_storage == NULL) {
		SDP_LOGD("Failed to get storage %d\n", tmp_storage->storage_id);
		return -1;
	}
	SDP_LOGD("Current User ID : %d\n", current_sdp_user->user_id);
	SDP_LOGD("Lock State : %s\n", (tmp_storage->lock_state == SDP_STATE_LOCKED) ? "Locked" : "Unlocked");
	SDP_LOGD("SDPK :\n");
	if (tmp_storage->sdpk != NULL) {
		ecryptfs_dump_hex(tmp_storage->sdpk->data, tmp_storage->sdpk->len);
	} else {
		SDP_LOGD("NULL\n");
	}
	SDP_LOGD("Storage ID : %d\n", tmp_storage->storage_id);
	return sprintf(buf, "%s\n", (tmp_storage->lock_state == SDP_STATE_LOCKED) ? "Locked" : "Unlocked");
}

static ssize_t sdp_set_locked_state(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count) {
	int flag = simple_strtoul(buf, NULL, 10);
	struct sdp_user *cur_user = sdp_get_current_user();
	if (flag == SDP_STATE_LOCKED) {
		sdp_storage_lock(cur_user, 0);
		ecryptfs_mm_drop_cache(0);
	} else if (flag == SDP_STATE_UNLOCKED) {
		sdp_storage_unlock(cur_user, 0, NULL);
	}
	return strlen(buf);
}

static ssize_t sdp_show_current_userid(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return sprintf(buf, "%d\n", current_sdp_user->user_id);
}

static ssize_t sdp_set_current_userid(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count) {
	int user_id = simple_strtoul(buf, NULL, 10);
	struct sdp_user *prev_user;
	prev_user = sdp_get_current_user();
	sdp_set_curret_user(user_id);
	ecryptfs_mm_drop_cache(prev_user->user_id);
	return strlen(buf);
}

static DEVICE_ATTR(locked_state, 0660, sdp_show_locked_state, sdp_set_locked_state);
static DEVICE_ATTR(current_userid, 0660, sdp_show_current_userid, sdp_set_current_userid);


static int sdp_create_sysfs_attr(struct device *d) {
	int error;

	if((error = device_create_file(d, &dev_attr_locked_state)))
		return error;
	if((error = device_create_file(d, &dev_attr_current_userid)))
		return error;

	return 0;
}


static int __init sdp_init(void) {
	int ret;

	ret = misc_register(&sdp_misc_ctl);
	if (unlikely(ret)) {
		SDP_LOGE("failed to register misc_evt device!\n");
		return ret;
	}

	ret = sdp_create_sysfs_attr(sdp_misc_ctl.this_device);
	if (unlikely(ret)) {
		SDP_LOGE("failed to create sysfs_asym_alg device!\n");
		return ret;
	}
	ret = sdp_init_user_list();
	if (unlikely(ret)) {
		SDP_LOGE("Failed to initialize user list");
		return ret;
	}

	SDP_LOGD("sdp: initialized\n");
	return 0;
}

static void __exit sdp_exit(void)
{
	printk("sdp: unloaded\n");
}

module_init(sdp_init)
module_exit(sdp_exit)

MODULE_LICENSE("GPL");

