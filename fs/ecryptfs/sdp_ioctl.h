#ifndef SDP_IOCTL_H_
#define SDP_IOCTL_H_

#include "ecryptfs_kernel.h"

#define __SDPIOC		        0x77

typedef struct _sdp_arg {
	int is_enabled;
	int is_sensitive;
}sdp_arg;

typedef struct _sdp_set_sensitive_params {
	int storage_id;
}sdp_set_sensitive_params;

typedef struct _sdp_lock_params {
	int storage_id;
}sdp_lock_params;

typedef struct _sdp_unlock_params {
	int storage_id;
	struct sdp_key key;
}sdp_unlock_params;

typedef struct _sdp_user_ctl_params {
	int user_id;
}sdp_user_ctl_params;

typedef struct _sdp_storage_ctl_params {
	int user_id;
	int storage_id;
}sdp_storage_ctl_params;

typedef struct _sdp_storage_info_params {
	int storage_id;
	int lock_state;
}sdp_storage_info_params;

enum sdp_op {
	TO_SENSITIVE = 0,
};

#define SDP_ON_DEVICE_UNLOCKED   _IOW(__SDPIOC, 0, unsigned int)
#define SDP_ON_DEVICE_LOCKED     _IOW(__SDPIOC, 1, unsigned int)
#define SDP_ON_ADD_USER   _IOW(__SDPIOC, 2, unsigned int)
#define SDP_ON_DEL_USER   _IOW(__SDPIOC, 3, unsigned int)
#define SDP_ON_CHANGE_USER   _IOW(__SDPIOC, 4, unsigned int)
#define SDP_ON_ADD_STORAGE   _IOW(__SDPIOC, 5, unsigned int)
#define SDP_ON_DEL_STORAGE   _IOW(__SDPIOC, 6, unsigned int)
#define SDP_ON_GET_STORAGE_INFO   _IOR(__SDPIOC, 7, unsigned int)
#define SDP_FILE_IOCTL_GET_INFO   _IOR(__SDPIOC, 8, unsigned int)
#define SDP_FILE_IOCTL_SET_SENSITIVE   _IOW(__SDPIOC, 9, unsigned int)


#endif //SDP_IOCTL_H_
