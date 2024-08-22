#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sys/time.h>

#include <RkBtBase.h>
#include <RkBtSink.h>
#include <RkBtSource.h>
#include <RkBle.h>
#include <RkBtSpp.h>
#include <RkBleClient.h>

//vendor code for broadcom
#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <sys/ioctl.h>
#include <glib.h>

enum{
	A2DP_SOURCE,
	A2DP_SINK
};

enum{
	ACL_NORMAL_PRIORITY,
	ACL_HIGH_PRIORITY
};
int vendor_set_high_priority(char *ba, uint8_t priority, uint8_t direction);
//vendor code for broadcom end

#include "bt_test.h"
#include "utility.h"

/* AD SERVICE_UUID */
#define AD_SERVICE_UUID16	"1111"
#define AD_SERVICE_UUID32	"00002222"
#define AD_SERVICE_UUID128	"00002222-0000-1000-8000-00805f9b34fb"

/* GAP/GATT Service UUID */
#define BLE_UUID_SERVICE	"00001111-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR	"00002222-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_SEND		"dfd4416e-1810-47f7-8248-eb8be3dc47f9"
#define BLE_UUID_RECV		"9884d812-1810-4a24-94d3-b2c11a851fac"
#define SERVICE_UUID		"00001910-0000-1000-8000-00805f9b34fb"

#define BLE_UUID_SERVICE1	"00001234-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_WIFI_CHAR1	"00001235-0000-1000-8000-00805F9B34FB"
#define BLE_UUID_SEND1		"00001236-1810-47f7-8248-eb8be3dc47f9"
#define BLE_UUID_RECV1		"00001237-1810-4a24-94d3-b2c11a851fac"
#define SERVICE_UUID1		"00001238-0000-1000-8000-00805f9b34fb"
#define TMP "00002a05-0000-1000-8000-00805f9b34fb"


/*Notification Source: UUID 9FBF120D-6301-42D9-8C58-25E699A21DBD (notifiable)*/
#define ANCS_NOTIFICATION_SOURCE "9FBF120D-6301-42D9-8C58-25E699A21DBD"
/* Control Point: UUID 69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9 (writeable with response) */
#define ANCS_CONTROL_POINT "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"
/* Data Source: UUID 22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB (notifiable) */
#define ANCS_DATA_SOURCE "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"

static struct timeval start, now;
static ssize_t totalBytes;

static void bt_test_ble_recv_data_callback(const char *uuid, char *data, int *len, RK_BLE_GATT_STATE state);

static int ble_direct_enable_adv(void);
static int ble_direct_close_adv(void);
static int ble_direct_set_adv_param(char *ble_name , char *ble_mac_addr);
static volatile bool ble_direct_flag = false;
/*
 * This structure must be initialized before use!
 *
 * The following variables will be updated by librkwifibt.so
 * bool init;
 * bool power;
 * bool pairable;
 * bool discoverable;
 * bool scanning;
 */
static RkBtContent bt_content;

/* BT base api */

void at_evt_callback(char *at_evt);

static char rfcomm_remote_address[18];
gboolean bt_open_rfcomm(gpointer data)
{
	rk_bt_rfcomm_open(rfcomm_remote_address, at_evt_callback);

	return false;
}

gboolean bt_reconect_last_dev(gpointer data)
{
	int i, count;
	struct remote_dev *rdev = NULL;
	printf("bt_reconect_last_dev\n");

	//Get all devices
	if (bt_get_devices(&rdev, &count) < 0) {
		printf("Can't get scan list!");
		return false;
	}

	if (count == 0)
		return false;

	printf("rdev: %p, count: %d\n", rdev, count);
	i = count - 1;
	for (; i >= 0; i--) {
		if (rdev[i].connected)
			printf("Connected Device %s (%s:%s)\n",
					rdev[i].remote_address,
					rdev[i].remote_address_type,
					rdev[i].remote_alias);
		else
			printf("%s Device %s (%s:%s)\n",
				rdev[i].paired ? "Paired" : "Scaned",
				rdev[i].remote_address,
				rdev[i].remote_address_type,
				rdev[i].remote_alias);

		if (!rdev[i].connected && rdev[i].paired) {
			//printf("Reconnect device %s\n", rdev[i].remote_address);
			//rk_adapter_connect(rdev[i].remote_address, NULL);
			//rk_bt_connect_by_addr(rdev[i].remote_address);
			return false;
		}
	}
	return false;
}

/*
 * !!!Never write or call delaying or blocking code or functions within this function.
 * !!!切勿在此函数内编写或调用延迟或阻塞的代码或函数。
 *
 * !!!The rdev of some events is NULL. You must determine whether rdev is NULLL, otherwise a crash will occur.
 * !!!某些event的rdev是NULL，必须判断rdev是否为NULLL,否则会出现crash
 */
static void bt_test_state_cb(RkBtRemoteDev *rdev, RK_BT_STATE state)
{
	switch (state) {
	//BASE STATE
	case RK_BT_STATE_TURNING_ON:
		printf("++ RK_BT_STATE_TURNING_ON\n");
		break;
	case RK_BT_STATE_INIT_ON:
		printf("++ RK_BT_STATE_INIT_ON\n");
		//only test
		//exec_command_system("hciconfig hci0 reset");
		break;
	case RK_BT_STATE_INIT_OFF:
		printf("++ RK_BT_STATE_INIT_OFF\n");
		break;

	//SCAN STATE
	case RK_BT_STATE_SCAN_NEW_REMOTE_DEV:
		if (rdev != NULL) {
			if (rdev->paired)
				printf("+ PAIRED_DEV: [%s|%d]:%s:%s\n", rdev->remote_address, rdev->rssi,
						rdev->remote_address_type, rdev->remote_alias);
			else
				printf("+ SCAN_NEW_DEV: [%s|%d]:%s:%s\n", rdev->remote_address, rdev->connected,
						rdev->remote_address_type, rdev->remote_alias);
		}
		break;
	case RK_BT_STATE_SCAN_CHG_REMOTE_DEV:
		if (rdev != NULL) {
			printf("+ SCAN_CHG_DEV: [%s|%d]:%s:%s|%s\n", rdev->remote_address, rdev->rssi,
					rdev->remote_address_type, rdev->remote_alias, rdev->change_name);

			if (!strcmp(rdev->change_name, "UUIDs")) {
				for (int index = 0; index < 36; index++) {
					if (!strcmp(rdev->remote_uuids[index], "NULL"))
						break;
					printf("\tUUIDs: %s\n", rdev->remote_uuids[index]);
				}
			} else if (!strcmp(rdev->change_name, "Icon")) {
				printf("\tIcon: %s\n", rdev->icon);
			} else if (!strcmp(rdev->change_name, "Class")) {
				printf("\tClass: 0x%x\n", rdev->cod);
			} else if (!strcmp(rdev->change_name, "Modalias")) {
				printf("\tModalias: %s\n", rdev->modalias);
			} else if (!strcmp(rdev->change_name, "MTU")) {
				printf("CONN_CHG_DEV att_mtu: %d\n", rdev->att_mtu);
			}
		}
		break;
	case RK_BT_STATE_SCAN_DEL_REMOTE_DEV:
		if (rdev != NULL)
			printf("+ SCAN_DEL_DEV: [%s]:%s:%s\n", rdev->remote_address,
					rdev->remote_address_type, rdev->remote_alias);
		break;

	//LINK STATE
	case RK_BT_STATE_CONNECTED:
	case RK_BT_STATE_DISCONN:
		if (rdev != NULL)
			printf("+ %s [%s|%d]:%s:%s\n", rdev->connected ? "STATE_CONNECTED" : "STATE_DISCONNECTED",
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias);

		if (state == RK_BT_STATE_CONNECTED && !strcmp(rdev->remote_address_type, "public")) {
				memcpy(bt_content.connected_a2dp_addr, rdev->remote_address, 18);
		}

		if (ble_direct_flag && (state == RK_BT_STATE_DISCONN)) {
			ble_direct_enable_adv();
		}

		break;
	case RK_BT_STATE_PAIRED:
	case RK_BT_STATE_PAIR_NONE:
		if (rdev != NULL)
			printf("+ %s [%s|%d]:%s:%s\n", rdev->paired ? "STATE_PAIRED" : "STATE_PAIR_NONE",
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias);
		break;
	case RK_BT_STATE_BONDED:
	case RK_BT_STATE_BOND_NONE:
		if (rdev != NULL)
			printf("+ %s [%s|%d]:%s:%s\n", rdev->bonded ? "STATE_BONDED" : "STATE_BOND_NONE",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
	case RK_BT_STATE_DEL_DEV_OK:
		if (rdev != NULL)
			printf("+ RK_BT_STATE_DEL_DEV_OK: %s:%s:%s\n",
				rdev->remote_address,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
	case RK_BT_STATE_BOND_FAILED:
	case RK_BT_STATE_PAIR_FAILED:
		printf("+ STATE_BOND/PAIR FAILED\n");
		break;

	case RK_BT_STATE_CONNECT_FAILED:
		if (rdev != NULL)
			printf("+ STATE_FAILED [%s|%d]:%s:%s reason: %s\n",
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias,
					rdev->change_name);
		break;
	case RK_BT_STATE_DISCONN_ALREADY:
		printf("+ STATE_DISCONNECTED: RK_BT_STATE_DISCONN_ALREADY\n");
		break;
	case RK_BT_STATE_DISCONN_FAILED:
		printf("+ STATE_FAILED: RK_BT_STATE_DISCONN_FAILED\n");
		break;

	case RK_BT_STATE_CONNECTED_ALREADY:
		printf("+ STATE_CONNECTED: RK_BT_STATE_CONNECTED_ALREADY\n");
		break;
	case RK_BT_STATE_CONNECT_FAILED_INVAILD_ADDR:
		printf("+ STATE_FAILED: RK_BT_STATE_CONNECT_FAILED_INVAILD_ADDR\n");
		break;
	case RK_BT_STATE_CONNECT_FAILED_NO_FOUND_DEVICE:
		printf("+ STATE_FAILED: RK_BT_STATE_CONNECT_FAILED_NO_FOUND_DEVICE\n");
		break;
	case RK_BT_STATE_CONNECT_FAILED_SCANNING:
		printf("+ STATE_FAILED: RK_BT_STATE_CONNECT_FAILED_SCANNING\n");
		break;

	case RK_BT_STATE_DEL_DEV_FAILED:
		printf("+ STATE_FAILED: RK_BT_STATE_DEL_DEV_FAILED\n");
		break;

	//MEDIA A2DP SOURCE
	case RK_BT_STATE_SRC_ADD:
	case RK_BT_STATE_SRC_DEL:
		if (rdev != NULL) {
			printf("+ STATE SRC MEDIA %s [%s|%d]:%s:%s\n",
					(state == RK_BT_STATE_SRC_ADD) ? "ADD" : "DEL",
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias);
			printf("+ codec: %s, freq: %s, chn: %s\n",
						rdev->media.codec == 0 ? "SBC" : "UNKNOW",
						rdev->media.sbc.frequency == 1 ? "48K" : "44.1K",
						rdev->media.sbc.channel_mode == 1 ? "JOINT_STEREO" : "STEREO");
		}
		break;

	//MEDIA AVDTP TRANSPORT
	case RK_BT_STATE_TRANSPORT_VOLUME:
		if (rdev != NULL)
			printf("+ STATE AVDTP TRASNPORT VOLUME[%d] [%s|%d]:%s:%s\n",
					rdev->media.volume,
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias);
		break;
	case RK_BT_STATE_TRANSPORT_IDLE:
		if (rdev != NULL) {
			printf("+ STATE AVDTP TRASNPORT IDLE [%s|%d]:%s:%s\n",
					rdev->remote_address,
					rdev->rssi,
					rdev->remote_address_type,
					rdev->remote_alias);
			//low priority for broadcom
			vendor_set_high_priority(rdev->remote_address, ACL_NORMAL_PRIORITY,
									 bt_content.profile & PROFILE_A2DP_SINK_HF ? A2DP_SINK : A2DP_SOURCE);
		}
		break;
	case RK_BT_STATE_TRANSPORT_PENDING:
		if (rdev != NULL)
			printf("+ STATE AVDTP TRASNPORT PENDING [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
	case RK_BT_STATE_TRANSPORT_ACTIVE:
		if (rdev != NULL) {
			printf("+ STATE AVDTP TRASNPORT ACTIVE [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
			//high priority for broadcom
			vendor_set_high_priority(rdev->remote_address, ACL_HIGH_PRIORITY,
								 bt_content.profile & PROFILE_A2DP_SINK_HF ? A2DP_SINK : A2DP_SOURCE);
		}
		break;
	case RK_BT_STATE_TRANSPORT_SUSPENDING:
		if (rdev != NULL)
			printf("+ STATE AVDTP TRASNPORT SUSPEND [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;

	//MEDIA A2DP SINK
	case RK_BT_STATE_SINK_ADD:
	case RK_BT_STATE_SINK_DEL:
		if (rdev != NULL) {
			printf("+ STATE SINK MEDIA %s [%s|%d]:%s:%s\n",
				(state == RK_BT_STATE_SINK_ADD) ? "ADD" : "DEL",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
			printf("+ codec: %s, freq: %s, chn: %s\n",
					rdev->media.codec == 0 ? "SBC" : "UNKNOW",
					rdev->media.sbc.frequency == 1 ? "48K" : "44.1K",
					rdev->media.sbc.channel_mode == 1 ? "JOINT_STEREO" : "STEREO");

			if (state == RK_BT_STATE_SINK_ADD) {
				//rk_bt_pbap_get_vcf(rdev->remote_address, "pb", "/data/pb.vcf");
				memset(rfcomm_remote_address, 0, 17);
				strncpy(rfcomm_remote_address, rdev->remote_address, 17);
				g_timeout_add(1000, bt_open_rfcomm, NULL);
				//rk_bt_rfcomm_open(rdev->remote_address, at_evt_callback);
			} else if (state == RK_BT_STATE_SINK_DEL) {
				//rk_bt_rfcomm_close();
			}
		}
		break;
	case RK_BT_STATE_SINK_PLAY:
		if (rdev != NULL)
			printf("+ STATE SINK PLAYER PLAYING [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
	case RK_BT_STATE_SINK_STOP:
		if (rdev != NULL)
			printf("+ STATE SINK PLAYER STOP [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
	case RK_BT_STATE_SINK_PAUSE:
		if (rdev != NULL)
			printf("+ STATE SINK PLAYER PAUSE [%s|%d]:%s:%s\n",
				rdev->remote_address,
				rdev->rssi,
				rdev->remote_address_type,
				rdev->remote_alias);
		break;
    case RK_BT_STATE_SINK_TRACK:
        printf("+ STATE SINK TRACK INFO [%s|%d]:%s:%s track[%s]-[%s]\n",
            rdev->remote_address,
            rdev->rssi,
            rdev->remote_address_type,
            rdev->remote_alias,
            rdev->title,
            rdev->artist);
    break;
    case RK_BT_STATE_SINK_POSITION:
        printf("+ STATE SINK TRACK POSITION:[%s|%d]:%s:%s [%u-%u]\n",
                rdev->remote_address,
                rdev->rssi,
                rdev->remote_address_type,
                rdev->remote_alias,
                rdev->player_position,
                rdev->player_total_len);
    break;

	//ADV
	case RK_BT_STATE_ADAPTER_BLE_ADV_START:
		bt_content.ble_content.ble_advertised = true;
		printf("RK_BT_STATE_ADAPTER_BLE_ADV_START successful\n");
		break;
	case RK_BT_STATE_ADAPTER_BLE_ADV_STOP:
		bt_content.ble_content.ble_advertised = false;
		printf("RK_BT_STATE_ADAPTER_BLE_ADV_STOP successful\n");
		break;

	//ADAPTER STATE
	case RK_BT_STATE_ADAPTER_NO_DISCOVERYABLED:
		bt_content.discoverable = false;
		printf("RK_BT_STATE_ADAPTER_NO_DISCOVERYABLED\n");
		//re-discoverable
		//rk_bt_set_discoverable(1);
		break;
	case RK_BT_STATE_ADAPTER_DISCOVERYABLED:
		bt_content.discoverable = true;
		printf("RK_BT_STATE_ADAPTER_DISCOVERYABLED\n");
		break;
	case RK_BT_STATE_ADAPTER_NO_PAIRABLED:
		bt_content.pairable = false;
		printf("RK_BT_STATE_ADAPTER_NO_PAIRABLED successful\n");
		break;
	case RK_BT_STATE_ADAPTER_PAIRABLED:
		bt_content.pairable = true;
		printf("RK_BT_STATE_ADAPTER_PAIRABLED successful\n");
		break;
	case RK_BT_STATE_ADAPTER_NO_SCANNING:
		bt_content.scanning = false;
		printf("RK_BT_STATE_ADAPTER_NO_SCANNING successful\n");
		break;
	case RK_BT_STATE_ADAPTER_SCANNING:
		bt_content.scanning = true;
		printf("RK_BT_STATE_ADAPTER_SCANNING successful\n");
		break;
	case RK_BT_STATE_ADAPTER_POWER_ON:
		bt_content.power = true;
		printf("RK_BT_STATE_ADAPTER_POWER_ON successful\n");
		//rk_bt_set_discoverable(1);
		break;
	case RK_BT_STATE_ADAPTER_POWER_OFF:
		bt_content.power = false;
		printf("RK_BT_STATE_ADAPTER_POWER_OFF successful\n");
		break;

	case RK_BT_STATE_COMMAND_RESP_OK:
		printf("RK_BT_STATE CMD OK\n");
		break;
	case RK_BT_STATE_COMMAND_RESP_ERR:
		printf("RK_BT_STATE CMD ERR\n");
		break;

	default:
		if (rdev != NULL)
			printf("+ DEFAULT STATE %d: %s:%s:%s RSSI: %d [CBP: %d:%d:%d]\n", state,
				rdev->remote_address,
				rdev->remote_address_type,
				rdev->remote_alias,
				rdev->rssi,
				rdev->connected,
				rdev->paired,
				rdev->bonded);
		break;
	}
}

void bt_test_version(char *data)
{
	printf("RK BT VERSION: %s\n", rk_bt_version());
}

void bt_test_source_play(char *data)
{
	char rsp[64], aplay[128];
	exec_command("hcitool con | grep ACL | awk '{print $3}'", rsp, 64);
	if (rsp[0]) {
		rsp[17] = 0;
		sprintf(aplay, "aplay -D bluealsa:DEV=%s,PROFILE=a2dp /data/test.wav", rsp);
		exec_command_system(aplay);
	}
}

/*
 * This function is a callback from the rk_bt_set_profile() API.
 * It gets called whenever the profile is changed.
 *
 * The function first checks if the bluealsa service is running
 * If it is, it unloads the bluetooth modules to avoid the
 * conflict with bluealsa.
 *
 * Next, it checks if bluealsa is running.
 * If it is, it kills the process to start a new one with updated profile.
 *
 * Depending on the profile, it starts either a2dp-sink,
 * hfp-hf or a2dp-source, hfp-ag with bluealsa.
 *
 * The function returns true if successful, false otherwise.
 */
static bool bt_test_audio_server_cb(bool enable)
{
	char rsp[64];

	/* Print the current profile set on the bt_content struct */
	printf("%s bt_content.profile: 0x%x, a2dp-sink: %d, a2dp-source: %d\n", __func__, bt_content.profile,
			(bt_content.profile & PROFILE_A2DP_SINK_HF),
			(bt_content.profile & PROFILE_A2DP_SOURCE_AG));

	if (enable == false) {
		/* stop necessary services */
		kill_task("bluealsa");
		kill_task("bluealsa-aplay");
		return true;
	}

	/* The pulseaudio is used. */
	if (bt_content.bluealsa == false)
		return true;

	/*
	 * If pulseaudio is running, unload the bluetooth modules
	 * to avoid the conflict with bluealsa.
	 */
	if (get_ps_pid("pulseaudio")) {
		exec_command("pactl list modules | grep bluetooth", rsp, 64);
		if (rsp[0]) {
			exec_command_system("pactl unload-module module-bluetooth-policy");
			exec_command_system("pactl unload-module module-bluetooth-discover");
		}
	}

	/* restart bluealsa */
	kill_task("bluealsa");
	kill_task("bluealsa-aplay");
	kill_task("pulseaudio");

	/*
	 * Start bluealsa service with the appropriate profile
	 * based on the profile set on bt_content struct
	 */
	if ((bt_content.profile & PROFILE_A2DP_SINK_HF) == PROFILE_A2DP_SINK_HF) {
		exec_command_system("bluealsa -S --profile=a2dp-sink --profile=hfp-hf &");
		exec_command_system("bluealsa-aplay -S --profile-a2dp 00:00:00:00:00:00 &");
	} else if ((bt_content.profile & PROFILE_A2DP_SOURCE_AG) == PROFILE_A2DP_SOURCE_AG) {
		exec_command_system("bluealsa -S --profile=a2dp-source --profile=hfp-ag --a2dp-volume &");
	}

	/* Wait for 100ms */
	usleep(100 * 1000);
	/* Reconnect last device */
	g_idle_add(bt_reconect_last_dev, NULL);

	return true;
}

/**
 * Callback function for setting vendor profile.
 *
 * This function is called when the vendor profile is changed.
 * It is responsible for starting and stopping the necessary services
 * based on the profile set on bt_content struct.
 *
 * @param enable true if the profile is being enabled, false otherwise
 *
 * @return true if successful, false otherwise
 * 
 * NOTE: That function must ensue that the hci0 node appears and the bluetoothd process is running.
 * NOTE: That function must ensue that the hci0 node appears and the bluetoothd process is running.
 * NOTE: That function must ensue that the hci0 node appears and the bluetoothd process is running.
 */
static bool bt_test_vendor_cb(bool enable)
{
	int times = 100;

	/* For buildroot OS:
	 *  The wifibt-init.sh or bt_init.sh used for hci0 init/stop
	 *  The /etc/init.d/S40bluetooth or /etc/init.d/S40bluetoothd used for bluetoothd init/stop
	 */
	bool Buildroot_OS = false;

	/* For Debian OS:
	 *  The script /usr/bin/wifibt-init.sh used for hci0 init/stop
	 *  The "systemctl start/stop bluetoothd" used for bluetoothd init/stop
	 */
	bool Debian_OS = false;

	/* For Custom: only for broadcom chip
	 * hci0 init/stop:
	 * 	exec_command_system("echo 0 > /sys/class/rfkill/rfkill0/state && sleep 0.5 && echo 1 > /sys/class/rfkill/rfkill0/state");
	 *  exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,2,0,0,0,0,0,3,0,0 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS1 &");
	 * 
	 * bluetoothd init/stop:
	 *  exec_command_system("/usr/libexec/bluetooth/bluetoothd -n -P battery -d &");
	 *  exec_command_system("killall bluetoothd");
	 */
	bool Custom_OS = false;

	//Custom_OS = true;
	Buildroot_OS = true;

	if (enable) {
		/* stop necessary services */
		kill_task("brcm_patchram_plus1");
		kill_task("rtk_hciattach");
		kill_task("bluetoothd");
		kill_task("obexd");

		if (1)
			exec_command_system("btmon -w /data/btsnoop.log > /dev/null &");

		/* waiting for hci0 exited */
		if (0) {
			times = 100;
			do {
				if (access("/sys/class/bluetooth/hci0", F_OK) != 0)
					break;
				usleep(100 * 1000);

				if (times == 0) {
					printf("hci0 not exited!\n");
					return false;
				}
			} while (times--);
		}

		/* Bluetooth Controller Init: firmware download and to create hci0 */
		if (Buildroot_OS) {
			if (!access("/usr/bin/wifibt-init.sh", F_OK))
				exec_command_system("/usr/bin/wifibt-init.sh");
			else if (!access("/usr/bin/bt_init.sh", F_OK))
				exec_command_system("/usr/bin/bt_init.sh");
		} if (Debian_OS) {
			exec_command_system("sudo systemctl restart bluetooth");
		} else if (Custom_OS) {
			/* Reset BT_REG_ON */
			exec_command_system("echo 0 > /sys/class/rfkill/rfkill0/state && sleep 0.5 && echo 1 > /sys/class/rfkill/rfkill0/state");
			/* 
			 * exec brcm_patchram_plus1 to init hci0 for broadcom chip!
			 *
			 * if hfp profile is used: (only for broadcom chip)
			 * if you use SCO PCM 8K: you should use：--scopcm=0,1,0,0,0,0,0,3,0,0
			 * if you use SCO PCM 16K(mSBC): you should use --scopcm=0,2,0,0,0,0,0,3,0,0
			 * 
			 * scopcm:
			 * sco_routing: sco_routing is 0 for PCM, 1 for Transport, 2 for Codec and 3 for I2S
			 * pcm_interface_rate is 0 for 128KBps, 1 for 256 KBps, 2 for 512KBps, 3 for 1024KBps, and 4 for 2048Kbps
			 * frame_type is 0 for short and 1 for long
			 * sync_mode is 0 for slave and 1 for master
			 * clock_mode is 0 for slave and 1 for master
			 * lsb_first is 0 for false aand 1 for true
			 * fill_bits is the value in decimal for unused bits
			 * fill_method is 0 for 0's and 1 for 1's, 2 for signed and 3 for programmable
			 * fill_num is the number or bits to fill
			 * right_justify is 0 for false and 1 for true
			 * 
			 * i2s: --i2s=0,0,1,2
			 * i2s_enable is 0 for disable and 1 for enable
			 * is_master is 0 for slave and 1 for master
			 * sample_rate is 0 for 8KHz, 1 for 16Khz and 2 for 4 KHz
			 * clock_rate is 0 for 128KHz, 1 for 256KHz, 2 for 512 KHz, 3 for 1024 KHz and 4 for 2048 KHz.
			 * 
			 * Realtek:
			 * 8KHZ PCM采样率
			 * 16bit data位
			 * slave模式（clk由主控提供)
			 * data采用msb模式
			 * short frame sync 短同步
			 * fs 下降沿开始发送和接收data
			 * fs 之间2个slot，然后有效数据在第一个slot
			 * clk 频率为：2 * 16 * 8 = 256KHZ
			 */
			//exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,1,0,1,1,0,0,3,0,0 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS1 &");
			//exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,2,0,1,1,0,0,3,0,0 --i2s=0,1,1,2 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS4 &");
			//exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,2,0,0,0,0,0,3,0,0 --i2s=0,0,1,2 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS1 &");
			//exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,2,0,1,1,0,0,3,0,0 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS4 &");
			exec_command_system("brcm_patchram_plus1 --enable_hci --scopcm=0,2,0,1,1,0,0,3,0,0 --i2s=0,1,1,2 --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS4 &");
			//exec_command_system("brcm_patchram_plus1 --enable_hci --no2bytes --use_baudrate_for_download --tosleep 200000 --baudrate 1500000 --patchram /lib/firmware/ /dev/ttyS1 &");
		}

		//waiting for hci0
		times = 100;
		do {
			if (access("/sys/class/bluetooth/hci0", F_OK) == 0)
				break;
			usleep(100 * 1000);

			if (times == 0) {
				printf("ERR: hci0 not init!\n");
				return false;
			}
		} while (times--);

		//exec_command_system("hciconfig hci0 reset");
		//exec_command_system("hciconfig hci0 down && sleep 3");

		/*
		 * Start bluetoothd
		 *
		 * DEBUG: vim /etc/init.d/S40bluetooth, modify BLUETOOTHD_ARGS="-n -d"

		 * if (access("/etc/init.d/S40bluetooth", F_OK) == 0)
		 * 	exec_command_system("/etc/init.d/S40bluetooth restart");
		 * else if (access("/etc/init.d/S40bluetoothd", F_OK) == 0)
		 * 	exec_command_system("/etc/init.d/S40bluetoothd restart");
		 */
		if (1) {
			//debug_mode
			//exec_command_system("/usr/libexec/bluetooth/bluetoothd -n -P battery &");
			exec_command_system("/usr/libexec/bluetooth/bluetoothd -d -n -P battery,hostname,gap,wiimote -f /data/main.conf &");
		} else
			exec_command_system("/usr/libexec/bluetooth/bluetoothd -n -P battery &");

		//waiting for bluetoothd
		times = 100;
		do {
			if (get_ps_pid("bluetoothd"))
				break;

			usleep(100 *1000);

			if (times == 0) {
				printf("bluetoothd not init!\n");
				return false;
			}
		} while (times--);

		//obexd
		if (bt_content.profile & PROFILE_OBEX) {
			exec_command_system("export $(dbus-launch)");
			exec_command_system("/usr/libexec/bluetooth/obexd -r /userdata/ -a -n &");
			//debug: exec_command_system("/usr/libexec/bluetooth/obexd -r /userdata/ -a -n -d &");
			//check bluetoothd
			times = 100;
			do {
				if (get_ps_pid("obexd"))
					break;
				usleep(100 *1000);

				if (times == 0) {
					printf("obexd not init!\n");
					return false;
				}
			} while (times--);
		}
	} else {
		//CLEAN
		exec_command_system("hciconfig hci0 down");
		exec_command_system("/etc/init.d/S40bluetooth stop");

		kill_task("bluetoothd");
		kill_task("obexd");

		//audio server deinit
		kill_task("bluealsa");
		kill_task("bluealsa-alay");

		//vendor deinit
		kill_task("brcm_patchram_plus1");
		kill_task("rtk_hciattach");
	}

	return true;
}

/*
reference: Assigned_Numbers.pdf

BT 5.X
#Command Code    LE Set Extended Advertising Disable Command
hcitool -i hci0 cmd 0x08 0x0039  00 01 01 00 00 00

#Command Code    LE Remove Advertising Set Command
hcitool -i hci0 cmd 0x08 0x003C 01

#Command Code    LE Set Extended Advertising Parameters Command
Advertising_Handle: 				0x01
Advertising_Event_Properties: 		0x0013     		//00010011  
													bit0: Connectable advertising
											 		bit1: Scannable advertising
											 		bit2: Directed advertising
													bit3: High Duty Cycle Directed Connectable advertising (≤ 3.75 ms Advertising Interval)
											 		bit4: Use Legacy advertising PDUs
											 		bit5: Omit advertiser's address from all PDUs ("anonymous advertising")
											 		bit6: Include TxPower in the extended header of at least one advertising PDU
Primary_Advertising_Interval_Min: 	0x0000AO		//Range: 0x000020 to 0xFFFFFF Time = N * 0.625 ms Time Range: 20 ms to 10,485.759375 s
Primary_Advertising_Interval_Max:	0x0000A0		//Range: 0x000020 to 0xFFFFFF Time = N * 0.625 ms Time Range: 20 ms to 10,485.759375 s
Primary_Advertising_Channel_Map:	0x07			//bit0: CHAN_37 bit1: CHAN_38 bit2: CHAN_39
Own_Address_Type:					0x01			//0x00: Public Device Address, 0x01: Random Device Address, 0x02/0x03：Controller generated ...
Peer_Address_Type:					0x00			//0x00 Public Device Address or Public Identity Address, 0x01: Random Device Address or Random (static) Identity Address
Peer_Address:						0x00		    //6byte
Advertising_Filter_Policy:			0x00			//0x00: Process scan and connection requests from all devices (i.e., the White List is not in use)
Advertising_TX_Power:				0x7F			//Range: -127 to +20, 0x7F: Host has no preference
Primary_Advertising_PHY:			0x01			//0x01: 1M, 0x03: Le Coded
Secondary_Advertising_Max_Skip:		0x00			//AUX_ADV_IND shall be sent prior to the next advertising event
Secondary_Advertising_PHY:			0x01			//0x01: 1M, 0x02: 2M, 0x03: Le Coded
Advertising_SID:					0x00			//0x00 to 0x0F Value of the Advertising SID subfield in the ADI field of the PDU
Scan_Request_Notification_Enable:	0x00			//0x00: Scan Request Notification is disabled, 0x01: Scan Request Notification is enabled

hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 A0 00 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 00 00

#Command Code    LE Set Advertising Set Random Address Command
hcitool -i hci0 cmd 0x08 0x0035 01 45 6E 87 2D 6A 44

#Command Code    LE Set Extended Advertising Data Command
Advertising_Handle: 	0x01
Options:				0x03
	Value Parameter Description
	0x00 Intermediate fragment of fragmented extended advertising data
	0x01 First fragment of fragmented extended advertising data
	0x02 Last fragment of fragmented extended advertising data
	0x03 Complete extended advertising data
	0x04 Unchanged data (just update the Advertising DID)
	All other values Reserved for future use
Fragment_Preference:	0x01
	Value Parameter Description
	0x00 The Controller may fragment all Host advertising data
	0x01 The Controller should not fragment or should minimize fragmentation of 
	Host advertising data
	All other values Reserved for future use

Advertising_Data_Length: 0 to 251 The number of octets in the Advertising Data parameter

Advertising_Data: Size: Advertising_Data_Length octets
hcitool -i hci0 cmd 0x08 0x0037 01 03 01 0x8 09 54 65 73 74 20 4C 46

#Command Code    LE Set Extended Scan Response Data command
hcitool -i hci0 cmd 0x08 0x0038

#Command Code    LE Set Extended Advertising Enable Command
hcitool -i hci0 cmd 0x08 0x0039  01 01 01 00 00 00


BT 4.X
Advertising_Interval_Min
Advertising_Interval_Max
Advertising_Type
Own_Address_Type			//0x00: Public Device Address, 
							//0x01: Random Device Address,
							//0x02: Controller generates Resolvable Private Address based on the local IRK from the resolving list.
									If the resolving list contains no matching entry, use the public address.
							//0x03: Controller generates Resolvable Private Address based on the local IRK from the resolving list
									If the resolving list contains no matching entry, use the random address from LE_Set_Random_Address
Peer_Address_Type
Peer_Address
Advertising_Channel_Map
Advertising_Filter_Policy

DEVICE ADDRESS
Devices are identified using a device address and an address type

A device shall use at least one type of device address and may contain both.

A device's Identity Address is a Public Device Address or Random Static 
Device Address that it uses in packets it transmits. If a device is using 
Resolvable Private Addresses, it shall also have an Identity Address.

Whenever two device addresses are compared, the comparison shall include 
the device address type (i.e. if the two addresses have different types, they are 
different even if the two 48-bit addresses are the same).

1/ Public device address
	The public device address shall be created in accordance with [Vol 2] Part B, 
	Section 1.2, with the exception that the restriction on LAP values does not 
	apply unless the public device address will also be used as a BD_ADDR for a 
	BR/EDR Controller.

2/ Random device address
	The random device address may be of either of the following:
	• Static address
	• Private address.

	Address [47:46] Sub-Type
	0b00 			Non-resolvable private address
	0b01 			Resolvable private address
	0b10 			Reserved for future use
	0b11 			Static device address
*/
//#define BLE_ADV_CUSTOM
static int ble_direct_set_adv_param(char *ble_name , char *ble_mac_addr)
{
#ifdef BLE_ADV_CUSTOM
	char cmd_para[128];
	char ret_buff[128];
	char temp[32];

	//using ble adv
	ble_direct_flag = true;

	//5.x
	//exec_command("hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 A0 00 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 00 00", ret_buff, 128);
	//char CMD_ADV_RESP_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0037 01 03 01";

	//4.x
	/* setup 1: set adv param (default Don't modify) */
	exec_command("hcitool -i hci0 cmd 0x08 0x0006 20 00 20 00 00 01 00 00 00 00 00 00 00 07 00", ret_buff, 128);
	printf("CMD_PARA ret buff: %s\n", ret_buff);

	//set ble macaddr
	memset(cmd_para, 0, 128);
	sprintf(cmd_para, "%s %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx",
			//"hcitool -i hci0 cmd 0x08 0x0035 01",
			"hcitool -i hci0 cmd 0x08 0x0005",
			ble_mac_addr[5], ble_mac_addr[4], ble_mac_addr[3],
			ble_mac_addr[2], ble_mac_addr[1], ble_mac_addr[0]);
	printf("CMD_MAC: %zu, %s\n", strlen(cmd_para), cmd_para);
	exec_command(cmd_para, ret_buff, 128);
	printf("CMD_MAC ret buff: %s\n", ret_buff);

	/* setup 2: ADV DATA 32 Bytes */
	//char CMD_ADV_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0008"; //在后面补32个字节的自定义数据，切记需要以字符串的形式写入！

	//todo
	char CMD_ADV_DATA[256] = "hcitool -i hci0 cmd 0x08 0x0008 0x18 0x11 0x07 0xD0 0x00 0x2D 0x12 0x1E 0x4B 0x0F 0xA4 0x99 0x4E 0xCE 0xB5 0x31 0xF4 0x05 0x79 0x02 0x0A 0x10 0x02 0x01 0x0A 0x00 0x00 0x00 0x00 0x00 0x00 0x00"; //在后面补32个字节的自定义数据，切记需要以字符串的形式写入！
	printf("CMD_ADV_DATA: %s\n", CMD_ADV_DATA);
	exec_command(CMD_ADV_DATA, ret_buff, 256);
	printf("CMD_ADV_DATA ret: %s\n", ret_buff);

	/* setup 3: ADV DATA RESP 32 Bytes */
	char CMD_ADV_RESP_DATA[128] = "hcitool -i hci0 cmd 0x08 0x0009";  //在后面补32个字节的自定义数据，切记需要以字符串的形式写入！
	//example: set ble name (例子仅仅设置了一个名字)
	memset(cmd_para, 0, 128);

	cmd_para[0] = strlen(ble_name) + 1 + 1; //caculate all length

	cmd_para[1] = strlen(ble_name) + 1;		//caculate ble name length
	cmd_para[2] = 0x09; 					//type: complete local name
	sprintf(cmd_para + 3, "%s", ble_name);	//set ble name
	//printf("cmd_para ret: %s\n", cmd_para);

	//append ble name to CMD_ADV_RESP_DATA
	memset(temp, 0, 32);
	for (int i = 0; i < strlen(cmd_para); i++) {
		sprintf(temp, "%02x", cmd_para[i]);
		strcat(CMD_ADV_RESP_DATA, " ");
		strcat(CMD_ADV_RESP_DATA, temp);
	}

	//left all set 00 （剩余不够32byte的全部补0）
	for (int i = 0; i < (32 - 3 - strlen(ble_name)); i++) {
		strcat(CMD_ADV_RESP_DATA, " ");
		strcat(CMD_ADV_RESP_DATA, "00");
	}

	printf("CMD_ADV_RESP_DATA: %s\n", CMD_ADV_RESP_DATA);
	exec_command(CMD_ADV_RESP_DATA, ret_buff, 128);
	printf("CMD_ADV_RESP_DATA ret: %s\n", ret_buff);
#endif

	return 1;
}

static int ble_direct_enable_adv(void)
{
#ifdef BLE_ADV_CUSTOM
	int ret_buff[32];

	//enable adv
	exec_command("hcitool -i hci0 cmd 0x08 0x000a 1", ret_buff, 128);
	printf("enable adv ret: %s\n", ret_buff);
#endif

	return 1;
}

static int ble_direct_close_adv(void)
{
#ifdef BLE_ADV_CUSTOM
	int ret_buff[32];

	ble_direct_flag = false;

	//disable adv
	exec_command("hcitool -i hci0 cmd 0x08 0x000a 0", ret_buff, 128);
	printf("disable adv ret: %s\n", ret_buff);
#endif

	return 1;
}

#define BT_CONF_DIR "/data/main.conf"
static int create_bt_conf(struct bt_conf *conf)
{
	FILE* fp;
	char cmdline[256] = {0};

	fp = fopen(BT_CONF_DIR, "wt+");
	if (NULL == fp)
		return -1;

	fputs("[General]\n", fp);

	//DiscoverableTimeout
	if (conf->discoverableTimeout) {
		sprintf(cmdline, "DiscoverableTimeout = %s\n", conf->discoverableTimeout);
		fputs(cmdline, fp);
	}

	//BleName
	if (conf->BleName) {
		sprintf(cmdline, "BleName = %s\n", conf->BleName);
		fputs(cmdline, fp);
	}

	//class
	if (conf->Class) {
		sprintf(cmdline, "Class = %s\n", conf->Class);
		fputs(cmdline, fp);
	}

	//SSP
	if (conf->ssp) {
		sprintf(cmdline, "SSP = %s\n", conf->ssp);
		fputs(cmdline, fp);
	}

	//mode
	if (conf->mode) {
		sprintf(cmdline, "ControllerMode = %s\n", conf->mode);
		fputs(cmdline, fp);
	}

	//default always
	conf->JustWorksRepairing = "always";
	sprintf(cmdline, "JustWorksRepairing = %s\n", conf->JustWorksRepairing);
	fputs(cmdline, fp);

	fputs("[GATT]\n", fp);
	//#Cache = always 
	fputs("Cache = always", fp);

	fclose(fp);

	system("cat /data/main.conf");
	return 0;
}

/* bt init */
void *bt_test_init(void *arg)
{
	struct bt_conf conf;
	RkBleGattService *gatt;

	/* 
	 * "read"
	 * "write"
	 * "indicate"
	 * "notify"
	 * "write-without-response"
	 * "encrypt-read"
	 */
	static char *chr_props[] = { "read", "write", "notify", "write-without-response", NULL };

	printf("%s \n", __func__);

	//Must determine whether Bluetooth is turned on
	if (rk_bt_is_open()) {
		printf("%s: already open \n", __func__);
		return NULL;
	}

	memset(&bt_content, 0, sizeof(RkBtContent));

	//BREDR CLASS BT NAME
	bt_content.bt_name = "Pixoo-audio";

	//BREDR PINCODE
	bt_content.pincode = "1234";

	//BLE NAME
	bt_content.ble_content.ble_name = "Pixoo-ble";

	//IO CAPABILITY
	bt_content.io_capability = IO_CAPABILITY_DISPLAYYESNO;

	//OBEX: OPP(File transfer)/PBAP/MAP
	//bt_content.profile |= PROFILE_OBEX;

	/*
	 * Only one can be enabled
	 * a2dp sink and hfp-hf
	 * a2dp source and hfp-ag
	 */
	bt_content.profile |= PROFILE_A2DP_SINK_HF;
	bt_content.bluealsa = true;

	// enable ble
	bt_content.profile |= PROFILE_BLE;
	if (bt_content.profile & PROFILE_BLE) {
		/* GATT SERVICE/CHARACTERISTIC */
		//SERVICE_UUID
		gatt = &(bt_content.ble_content.gatt_instance[0]);
		gatt->server_uuid.uuid = SERVICE_UUID;
		gatt->chr_uuid[0].uuid = BLE_UUID_SEND;
		gatt->chr_uuid[0].chr_props = chr_props;

		gatt->chr_uuid[1].uuid = BLE_UUID_RECV;
		gatt->chr_uuid[1].chr_props = chr_props;
		gatt->chr_cnt = 2;

		//SERVICE_UUID1
		gatt = &(bt_content.ble_content.gatt_instance[1]);
		gatt->server_uuid.uuid = SERVICE_UUID1;
		gatt->chr_uuid[0].uuid = BLE_UUID_SEND1;
		gatt->chr_uuid[0].chr_props = chr_props;
		gatt->chr_uuid[1].uuid = BLE_UUID_RECV1;
		gatt->chr_uuid[1].chr_props = chr_props;
		gatt->chr_cnt = 2;

		bt_content.ble_content.srv_cnt = 2;

		/* Fill adv data */
		/*
		BT 4.X
		//LE Set Random Address Command
		hcitool -i hci0 cmd 0x08 0x0005 41 C5 10 C3 9C 04

		//LE SET PARAMETERS
		hcitool -i hci0 cmd 0x08 0x0006 A0 00 A0 00 00 01 00 00 00 00 00 00 00 07 00

		// LE Set Advertising Data Command
		hcitool -i hci0 cmd 0x08 0x0008 1b 02 01 02 03 03 10 19 13 09 52 4f 43 4b 43 48 49 50 5f 41 55 44 49 4f 5f 42 4c 45

		// LE Set Advertising Resp Data Command
		hcitool -i hci0 cmd 0x08 0x0009 17 16 ff 46 00 02 1c 02 04 54 01 00 00 08 54 00 00 00 00 00 00 36 01 00

		// LE Set Advertise Enable/Disable Command
		hcitool -i hci0 cmd 0x08 0x000a 1

		BT 5.X
		#Command Code    LE Set Extended Advertising Disable Command
		hcitool -i hci0 cmd 0x08 0x0039  00 01 01 00 00 00

		#Command Code    LE Remove Advertising Set Command
		hcitool -i hci0 cmd 0x08 0x003C 01

		#Command Code    LE Set Extended Advertising Parameters Command
		hcitool -i hci0 cmd 0x08 0x0036 01 13 00 A0 00 00 A0 00 00 07 01 00 00 00 00 00 00 00 00 7F 01 00 01 00 00

		#Command Code    LE Set Advertising Set Random Address Command
		hcitool -i hci0 cmd 0x08 0x0035 01 45 6E 87 2D 6A 44

		#Command Code    LE Set Extended Advertising Data Command
		hcitool -i hci0 cmd 0x08 0x0037 01 03 01 0D 03 03 0D 18 08 09 54 65 73 74 20 4C 46

		#Command Code    LE Set Extended Advertising Enable Command
		hcitool -i hci0 cmd 0x08 0x0039  01 01 01 00 00 00
		 */

		/* Appearance */
		bt_content.ble_content.Appearance = 0x0080;

		/* Tx power */
		//bt_content.ble_content.tx_power = 0x00;

		/* manufacturer data */
		bt_content.ble_content.manufacturer_id = 0x0059;
		for (int i = 0; i < 16; i++)
			bt_content.ble_content.manufacturer_data[i] = i;

		/* Service UUID */
		bt_content.ble_content.adv_server_uuid.uuid = AD_SERVICE_UUID16;

		//callback
		bt_content.ble_content.cb_ble_recv_fun = bt_test_ble_recv_data_callback;
	}

	rk_bt_register_state_callback(bt_test_state_cb);
	rk_bt_register_vendor_callback(bt_test_vendor_cb);
	rk_bt_register_audio_server_callback(bt_test_audio_server_cb);

	pthread_mutex_init(&bt_content.bt_mutex, NULL);
	pthread_cond_init(&bt_content.cond, NULL);

	//default state
	bt_content.init = false;
	bt_content.connecting = false;
	bt_content.scanning = false;
	bt_content.discoverable = false;
	bt_content.pairable = false;
	bt_content.power = false;

	//bt config file
	memset(&conf, 0, sizeof(struct bt_conf));
	//both BR/EDR and LE enabled, "dual", "le" or "bredr"
	conf.mode = "dual";
	//0 = disable timer, i.e. stay discoverable forever
	conf.discoverableTimeout = "0";
	//"audio-headset"
	conf.Class = "0x240414";
	//
	conf.BleName = bt_content.ble_content.ble_name;
	create_bt_conf(&conf);

	rk_bt_init(&bt_content);

	while (!bt_content.init)
		sleep(1);
	printf("bt init pass\n");

	printf("ble adv start\n");
	rk_ble_adv_start();

	//TEST ONLY
	//初始化时间和计数器
	//gettimeofday(&start, NULL);
	//totalBytes = 0;

	return NULL;
}

void bt_test_bluetooth_onoff_init(char *data)
{
	int test_cnt = 5000, cnt = 0;

	if (data)
		test_cnt = atoi(data);
	printf("%s test times: %d(%d)\n", __func__, test_cnt, data ? atoi(data) : 0);

	while (cnt < test_cnt) {
		printf("BT TEST INIT START\n");
		bt_test_init(NULL);
		while (bt_content.init == false) {
			sleep(1);
			printf("BT TURNING ON ...\n");
		}

		//scan test
		rk_bt_start_discovery(SCAN_TYPE_AUTO);
		while (bt_content.scanning == false) {
			sleep(1);
			printf("BT SCAN ON ...\n");
		}
		sleep(10);
		rk_bt_cancel_discovery();
		while (bt_content.scanning == true) {
			sleep(1);
			printf("BT SCAN OFF ...\n");
		}

		//ble adv tests
		rk_ble_adv_start();
		while (bt_content.ble_content.ble_advertised == false) {
			sleep(1);
			printf("BT ADV ON ...\n");
		}
		sleep(3);
		rk_ble_adv_stop();
		while (bt_content.ble_content.ble_advertised == true) {
			sleep(1);
			printf("BT ADV OFF ...\n");
		}

		rk_bt_deinit();
		while (bt_content.init == true) {
			sleep(1);
			printf("BT TURNING OFF ...\n");
		}
		printf("BT INIT/ADV/SCAN CNTs: [====== %d ======] \n", ++cnt);
	}
}

void bt_test_bluetooth_init(char *data)
{
	bt_test_init(NULL);

	return;
}

void bt_test_bluetooth_deinit(char *data)
{
	rk_bt_deinit();

	return;
}

void bt_test_get_adapter_info(char *data)
{
	rk_bt_adapter_info(data);
}

void bt_test_connect_by_addr(char *data)
{
	rk_bt_connect_by_addr(data);
}

void bt_test_disconnect_by_addr(char *data)
{
	rk_bt_disconnect_by_addr(data);
}

void bt_test_pair_by_addr(char *data)
{
	rk_bt_pair_by_addr(data);
}

void bt_test_unpair_by_addr(char *data)
{
	rk_bt_unpair_by_addr(data);
}

void bt_test_start_discovery(char *data)
{
	RK_BT_SCAN_TYPE type;

	if (data == NULL) {
		rk_bt_start_discovery(SCAN_TYPE_AUTO);
		return;
	}

	if (!strcmp(data, "bredr"))
		type = SCAN_TYPE_BREDR;
	else if (!strcmp(data, "le"))
		type = SCAN_TYPE_LE;
	else
		type = SCAN_TYPE_AUTO;

	rk_bt_start_discovery(type);
}

void bt_test_cancel_discovery(char *data)
{
	if (bt_content.scanning == false)
		return;

	rk_bt_cancel_discovery();
}

void bt_test_set_discoverable(char *data)
{
	bool enable;

	if (data == NULL)
		return;

	if (!strcmp(data, "on"))
		enable = 1;
	else if (!strcmp(data, "off"))
		enable = 0;
	else
		return;

	rk_bt_set_discoverable(enable);
}

void bt_test_set_pairable(char *data)
{
	bool enable;

	if (data == NULL)
		return;

	if (!strcmp(data, "on"))
		enable = 1;
	else if (!strcmp(data, "off"))
		enable = 0;
	else
		return;

	rk_bt_set_pairable(enable);
}

void bt_test_set_power(char *data)
{
	bool enable;

	if (data == NULL)
		return;

	if (!strcmp(data, "on"))
		enable = 1;
	else if (!strcmp(data, "off"))
		enable = 0;
	else
		return;

	rk_bt_set_power(enable);
}

void bt_test_get_all_devices(char *data)
{
	int i, count;
	struct remote_dev *rdev = NULL;

	//Get all devices
	if (bt_get_devices(&rdev, &count) < 0) {
		printf("Can't get scan list!");
		return;
	}

	printf("rdev: %p\n", rdev);
	for (i = 0; i < count; i++) {
		if (rdev[i].connected)
			printf("Connected Device %s (%s:%s)\n",
					rdev[i].remote_address,
					rdev[i].remote_address_type,
					rdev[i].remote_alias);
		else
			printf("%s Device %s (%s:%s)\n",
				rdev[i].paired ? "Paired" : "Scaned",
				rdev[i].remote_address,
				rdev[i].remote_address_type,
				rdev[i].remote_alias);
	}
}

static const char *class_to_icon(uint32_t class)
{
	switch ((class & 0x1f00) >> 8) {
	case 0x01:
		return "computer";
	case 0x02:
		switch ((class & 0xfc) >> 2) {
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x05:
			return "phone";
		case 0x04:
			return "modem";
		}
		break;
	case 0x03:
		return "network-wireless";
	case 0x04:
		switch ((class & 0xfc) >> 2) {
		case 0x01:
		case 0x02:
			return "audio-headset";
		case 0x06:
			return "audio-headphones";
		case 0x0b: /* VCR */
		case 0x0c: /* Video Camera */
		case 0x0d: /* Camcorder */
			return "camera-video";
		default:
			return "audio-card";	/* Other audio device */
		}
		break;
	case 0x05:
		switch ((class & 0xc0) >> 6) {
		case 0x00:
			switch ((class & 0x1e) >> 2) {
			case 0x01:
			case 0x02:
				return "input-gaming";
			}
			break;
		case 0x01:
			return "input-keyboard";
		case 0x02:
			switch ((class & 0x1e) >> 2) {
			case 0x05:
				return "input-tablet";
			default:
				return "input-mouse";
			}
		}
		break;
	case 0x06:
		if (class & 0x80)
			return "printer";
		if (class & 0x20)
			return "camera-photo";
		break;
	}

	return NULL;
}

void bt_test_read_remote_device_info(char *data)
{
	struct remote_dev rdev;
	char *t_addr = data;
	char *ios = "";

	if (bt_get_dev_info(&rdev, t_addr) < 0)
		return;

	ios = strstr(rdev.modalias, "v");
	if (ios) {
			if (!strncasecmp(ios + 1, "004c", 4) ||
			    !strncasecmp(ios + 1, "05ac", 4))
				ios = "Apple iOS system";
			else
				ios = "";
	}

	printf("Device info: addr:%s:%s, name: %s, class:(0x%x|%s|%s), appearance: 0x%x, RSSI: %d\n",
			rdev.remote_address,
			rdev.remote_address_type,
			rdev.remote_alias,
			rdev.cod, class_to_icon(rdev.cod), ios,
			rdev.appearance,
			rdev.rssi);
	printf("Supported UUIDs: \n");
	for (int index = 0; index < 10; index++) {
		if (!strcmp(rdev.remote_uuids[index], "NULL"))
			break;
		printf("	UUIDs: %s\n", rdev.remote_uuids[index]);
	}
}

/******************************************/
/*               A2DP SINK                */
/******************************************/
void bt_test_sink_media_control(char *data)
{
	rk_bt_sink_media_control(data);
}

void bt_test_a2dp_test_volume(char *data)
{
	if (data) {
		printf("===== A2DP Set Volume: %d =====\n", atoi(data));
		rk_bt_sink_set_volume(atoi(data));
	}
}

void bt_test_enable_a2dp_source(char *data)
{
	bool enable;

	if (data == NULL)
		return;

	if (!strcmp(data, "on"))
		enable = 1;
	else if (!strcmp(data, "off"))
		enable = 0;
	else
		return;

	rk_bt_set_profile(PROFILE_A2DP_SOURCE_AG, enable);
}

void bt_test_enable_a2dp_sink(char *data)
{
	bool enable;

	if (data == NULL)
		return;

	if (!strcmp(data, "on"))
		enable = 1;
	else if (!strcmp(data, "off"))
		enable = 0;
	else
		return;

	rk_bt_set_profile(PROFILE_A2DP_SINK_HF, enable);
}

/******************************************/
/*                  BLE                   */
/******************************************/
static void bt_test_ble_recv_data_callback(const char *uuid, char *data, int *len, RK_BLE_GATT_STATE state)
{
	switch (state) {
	//SERVER ROLE
	case RK_BLE_GATT_SERVER_READ_BY_REMOTE:
		//The remote dev reads characteristic and put data to *data.
		printf("+++ ble server is read by remote uuid: %s\n", uuid);
		*len = 247;//sstrlen("hello rockchip");
		//memcpy(data, "hello rockchip", strlen("hello rockchip"));
		memcpy(data, "hello rockchip 251", 247);
		break;
	case RK_BLE_GATT_SERVER_WRITE_BY_REMOTE:
		//The remote dev writes data to characteristic so print there.
		printf("+++ ble server is writeen by remote uuid: %s\n", uuid);
		for (int i = 0 ; i < *len; i++) {
			printf("%02x ", data[i]);
		}
		printf("\n");
		break;
	case RK_BLE_GATT_SERVER_ENABLE_NOTIFY_BY_REMOTE:
	case RK_BLE_GATT_SERVER_DISABLE_NOTIFY_BY_REMOTE:
		//The remote dev enable notify for characteristic
		printf("+++ ble server notify is %s by remote uuid: %s\n",
				(state == RK_BLE_GATT_SERVER_ENABLE_NOTIFY_BY_REMOTE) ? "enable" : "disabled",
				uuid);
		break;
	case RK_BLE_GATT_MTU:
		//
		printf("+++ ble server MTU: %d ===\n", *(uint16_t *)data);
		break;
	case RK_BLE_GATT_SERVER_INDICATE_RESP_BY_REMOTE:
		//The service sends notify to remote dev and recvs indicate from remote dev.
		printf("+++ ble server receive remote indicate resp uuid: %s\n", uuid);
		break;

	//CLIENT ROLE
	case RK_BLE_GATT_SERVER_READ_NOT_PERMIT_BY_REMOTE:
		//error handle: org.bluez.Error.NotPermitted
		printf("+++ ble client recv error: %s +++\n", data);
	case RK_BLE_GATT_CLIENT_READ_BY_LOCAL:
		printf("+++ ble client recv from remote: data uuid:%s, len:%d+++\n", uuid, *len);
		//for (int i = 0 ; i < *len; i++) {
		//	printf("%02x ", data[i]);
		//}
		//printf("\n");

		/*
		//printf("%02x %02x %02x \n", data[0], data[123], data[246]);
		//account data rate
		totalBytes += *len * 8; // 转换为位
		gettimeofday(&now, NULL);
		long elapsed = (now.tv_sec - start.tv_sec) * 1000000 + now.tv_usec - start.tv_usec;
		if (elapsed >= 1000000) { // 每秒计算一次
			printf("Rate: %ld bits/sec [%s]\n", totalBytes / (elapsed / 1000000), uuid);
			totalBytes = 0; // 重置计数器
			start = now; // 重置时间
		}
		*/
		break;
	case RK_BLE_GATT_CLIENT_WRITE_RESP_BY_LOCAL:
		//printf("+++ ble client recv write resp msg: %p %s\n", data, data ? data : "NULL");
		break;
	case RK_BLE_GATT_CLIENT_NOTIFY_ENABLE:
	case RK_BLE_GATT_CLIENT_NOTIFY_DISABLE:
		printf("+++ ble client uuid: %s notify is %s \n",
				uuid,
				(state == RK_BLE_GATT_CLIENT_NOTIFY_ENABLE) ? "enable" : "disabled"
				);
		break;
	case RK_BLE_GATT_CLIENT_NOTIFY_ERR:
		printf("RK_BLE_GATT_CLIENT_NOTIFY_ERR\n");
	default:
		break;
	}
}

void bt_test_ble_start(char *data)
{
#ifndef BLE_ADV_CUSTOM
	//bt_content.ble_content.ble_name = "RK_BLE";

	//bt_content.ble_content.manufacturer_id = 0x0059;
	//for (int i = 0; i < 16; i++)
	//	bt_content.ble_content.manufacturer_data[i] = i + "A";
	rk_ble_adv_start();
#else
	char mac[6];

	//custom ble macaddr
	//F9:57:15:D8:F4:05
	mac[5] = 0x05;
	mac[4] = 0xF4;
	mac[3] = 0xD8;
	mac[2] = 0x15;
	mac[1] = 0x57;
	mac[0] = 0xF9;

	//70:4A:0E:6C:AC:F5
	mac[5] = 0xF5;
	mac[4] = 0xAC;
	mac[3] = 0x6C;
	mac[2] = 0x0E;
	mac[1] = 0x4A;
	mac[0] = 0x70;

	ble_direct_set_adv_param("Pixoo-le", mac);
	ble_direct_enable_adv();
#endif
}

void bt_test_ble_set_adv_interval(char *data)
{
	//default 100ms, test: 20ms(32 * 0.625) ~ 100ms(160 * 0.625)
	//rk_ble_set_adv_interval(32, 160);
}

void bt_test_ble_write(char *data)
{
	rk_ble_send_notify("dfd4416e-1810-47f7-8248-eb8be3dc47f9", data, 4);
}

//ONLY FOR V1.6.1
void bt_test_ble_service_changed(char *data)
{
	char value[4];
	value[0] = 0x01;
	value[1] = 0x00;
	value[2] = 0xFF;
	value[3] = 0xFF;
	rk_ble_send_notify("dfd4416e-1810-47f7-8248-eb8be3dc47f9", value, 4);
}

void bt_test_ble_get_status(char *data)
{

}

void bt_test_ble_stop(char *data) 
{
#ifdef BLE_ADV_CUSTOM
	ble_direct_close_adv();
#else
	rk_ble_adv_stop();
#endif
}

/******************************************/
/*               BLE CLIENT               */
/******************************************/
void bt_test_ble_client_get_service_info(char *data)
{
	int i, j, k;
	RK_BLE_CLIENT_SERVICE_INFO info;

	if (!rk_ble_client_get_service_info(data, &info)) {
		printf("+++++ get device(%s) service info +++++\n", data);
		for(i = 0; i < info.service_cnt; i++) {
			printf("service[%d]:\n", i);
			printf("	describe: %s\n", info.service[i].describe);
			printf("	path: %s\n", info.service[i].path);
			printf("	uuid: %s\n", info.service[i].uuid);

			for(j = 0; j < info.service[i].chrc_cnt; j++) {
				printf("	characteristic[%d]:\n", j);
				printf("		describe: %s\n", info.service[i].chrc[j].describe);
				printf("		path: %s\n", info.service[i].chrc[j].path);
				printf("		uuid: %s\n", info.service[i].chrc[j].uuid);
				printf("		props: 0x%x\n", info.service[i].chrc[j].props);
				printf("		ext_props: 0x%x\n", info.service[i].chrc[j].ext_props);
				printf("		perm: 0x%x\n", info.service[i].chrc[j].perm);
				printf("		notifying: %d\n", info.service[i].chrc[j].notifying);

				for(k = 0; k < info.service[i].chrc[j].desc_cnt; k++) {
					printf("		descriptor[%d]:\n", k);

					printf("			describe: %s\n", info.service[i].chrc[j].desc[k].describe);
					printf("			path: %s\n", info.service[i].chrc[j].desc[k].path);
					printf("			uuid: %s\n", info.service[i].chrc[j].desc[k].uuid);
				}
			}
		}
	}
}

void bt_test_ble_client_read(char *data)
{
	rk_ble_client_read(data);
}

void bt_test_ble_client_write(char *data)
{
	char *write_buf = "hello world";

	rk_ble_client_write(data, write_buf, strlen("hello world"));
}

void bt_test_ble_client_is_notify(char *data)
{
	bool notifying;

	notifying = rk_ble_client_is_notifying(BLE_UUID_SEND);
	printf("%s notifying %s\n", BLE_UUID_SEND, notifying ? "yes" : "no");
}

void bt_test_ble_client_notify_on(char *data)
{
	rk_ble_client_notify(BLE_UUID_SEND, true);
}

void bt_test_ble_client_notify_off(char *data)
{
	rk_ble_client_notify(BLE_UUID_SEND, false);
}

void bt_test_ble_client_enable_ancs(char *data)
{
	bool enable;

	if (data == NULL) {
		printf("Invaild param! (xx input on/off)\n");
		return;
	}

	if (!strcmp(data, "on"))
		enable = true;
	else if (!strcmp(data, "off"))
		enable = false;
	else
		return;

	rk_ble_client_ancs(enable);
}

/******************************************/
/*                  SPP                   */
/******************************************/
void _btspp_status_callback(RK_BT_SPP_STATE type)
{
	switch(type) {
	case RK_BT_SPP_STATE_IDLE:
		printf("+++++++ RK_BT_SPP_STATE_IDLE +++++\n");
		break;
	case RK_BT_SPP_STATE_CONNECT:
		printf("+++++++ RK_BT_SPP_EVENT_CONNECT +++++\n");
		break;
	case RK_BT_SPP_STATE_DISCONNECT:
		printf("+++++++ RK_BT_SPP_EVENT_DISCONNECT +++++\n");
		break;
	default:
		printf("+++++++ BT SPP NOT SUPPORT TYPE! +++++\n");
		break;
	}
}

void _btspp_recv_callback(char *data, int len)
{
	if (len) {
		printf("+++++++ RK BT SPP RECV DATA: +++++\n");
		printf("\tRECVED(%d):%s\n", len, data);
	}
}

void bt_test_spp_open(char *data)
{
	rk_bt_spp_open(data);
	rk_bt_spp_register_status_cb(_btspp_status_callback);
	rk_bt_spp_register_recv_cb(_btspp_recv_callback);
}

void bt_test_spp_write(char *data)
{
	unsigned int ret = 0;
	//char buff[100] = {"This is a message from rockchip board!"};

	ret = rk_bt_spp_write(data, strlen(data));
	if (ret < 0) {
		printf("%s failed\n", __func__);
	}
}

void bt_test_spp_connect(char *data)
{
	rk_bt_spp_connect(data);
}

void bt_test_spp_disconnect(char *data)
{
	rk_bt_spp_disconnect(data);
}

void bt_test_spp_listen(char *data)
{
	rk_bt_spp_listen();
}

void bt_test_spp_close(char *data)
{
	rk_bt_spp_close();
}

void bt_test_spp_status(char *data)
{
	RK_BT_SPP_STATE status;

	rk_bt_spp_get_state(&status);
	switch(status) {
	case RK_BT_SPP_STATE_IDLE:
		printf("+++++++ RK_BT_SPP_STATE_IDLE +++++\n");
		break;
	case RK_BT_SPP_STATE_CONNECT:
		printf("+++++++ RK_BT_SPP_STATE_CONNECT +++++\n");
		break;
	case RK_BT_SPP_STATE_DISCONNECT:
		printf("+++++++ RK_BT_SPP_STATE_DISCONNECT +++++\n");
		break;
	default:
		printf("+++++++ BTSPP NO STATUS SUPPORT! +++++\n");
		break;
	}
}

/**
 * VENDOR CODE
 */
static int write_flush_timeout(int fd, uint16_t handle,
		unsigned int timeout_ms)
{
	uint16_t timeout = (timeout_ms * 1000) / 625;  // timeout units of 0.625ms
	unsigned char hci_write_flush_cmd[] = {
		0x01,               // HCI command packet
		0x28, 0x0C,         // HCI_Write_Automatic_Flush_Timeout
		0x04,               // Length
		0x00, 0x00,         // Handle
		0x00, 0x00,         // Timeout
	};

	hci_write_flush_cmd[4] = (uint8_t)handle;
	hci_write_flush_cmd[5] = (uint8_t)(handle >> 8);
	hci_write_flush_cmd[6] = (uint8_t)timeout;
	hci_write_flush_cmd[7] = (uint8_t)(timeout >> 8);

	int ret = write(fd, hci_write_flush_cmd, sizeof(hci_write_flush_cmd));
	if (ret < 0) {
		printf("write(): %s (%d)]", strerror(errno), errno);
		return -1;
	} else if (ret != sizeof(hci_write_flush_cmd)) {
		printf("write(): unexpected length %d", ret);
		return -1;
	}
	return 0;
}

static int vendor_high_priority(int fd, uint16_t handle,uint8_t priority, uint8_t direction)
{
	unsigned char hci_high_priority_cmd[] = {
		0x01,               // HCI command packet
		0x1a, 0xfd,         // Write_A2DP_Connection
		0x04,               // Length
		0x00, 0x00,         // Handle
		0x00, 0x00          // Priority, Direction 
	};

	hci_high_priority_cmd[4] = (uint8_t)handle;
	hci_high_priority_cmd[5] = (uint8_t)(handle >> 8);
	hci_high_priority_cmd[6] = (uint8_t)priority;
	hci_high_priority_cmd[7] = (uint8_t)direction; 

	int ret = write(fd, hci_high_priority_cmd, sizeof(hci_high_priority_cmd));
	if (ret < 0) {
		printf("write(): %s (%d)]", strerror(errno), errno);
		return -1;
	} else if (ret != sizeof(hci_high_priority_cmd)) {
		printf("write(): unexpected length %d", ret);
		return -1;
	}
	return 0;
}

static int get_hci_sock(void)
{
	int sock = socket(AF_BLUETOOTH, SOCK_RAW, BTPROTO_HCI);
	struct sockaddr_hci addr;
	int opt;

	if (sock < 0) {
		printf("Can't create raw HCI socket!");
		return -1;
	}

	opt = 1;
	if (setsockopt(sock, SOL_HCI, HCI_DATA_DIR, &opt, sizeof(opt)) < 0) {
		printf("Error setting data direction\n");
		return -1;
	}

	/* Bind socket to the HCI device */
	memset(&addr, 0, sizeof(addr));
	addr.hci_family = AF_BLUETOOTH;
	addr.hci_dev = 0;  // hci0
	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		printf("Can't attach to device hci0. %s(%d)\n",
				strerror(errno),
				errno);
		return -1;
	}
	return sock;
}

static int get_acl_handle(int fd, char *bdaddr) {
	int i;
	int ret = -1;
	struct hci_conn_list_req *conn_list;
	struct hci_conn_info *conn_info;
	int max_conn = 10;
	char addr[18];

	conn_list = malloc(max_conn * (
		sizeof(struct hci_conn_list_req) + sizeof(struct hci_conn_info)));
	if (!conn_list) {
		printf("Out of memory in %s\n", __FUNCTION__);
		return -1;
	}

	conn_list->dev_id = 0;  /* hardcoded to HCI device 0 */
	conn_list->conn_num = max_conn;

	if (ioctl(fd, HCIGETCONNLIST, (void *)conn_list)) {
		printf("Failed to get connection list\n");
		goto out;
	}
	printf("%s conn_num: %d\n", __func__, conn_list->conn_num);

	for (i=0; i < conn_list->conn_num; i++) {
		conn_info = &conn_list->conn_info[i];
		memset(addr, 0, 18);
		sprintf(addr, "%02x:%02x:%02x:%02x:%02x:%02x",
				conn_info->bdaddr.b[5],
				conn_info->bdaddr.b[4],
				conn_info->bdaddr.b[3],
				conn_info->bdaddr.b[2],
				conn_info->bdaddr.b[1],
				conn_info->bdaddr.b[0]);
		printf("%s: conn type: %d add: %s:%s\n", __func__, conn_info->type, bdaddr, addr);
		if (conn_info->type == ACL_LINK &&
				!strcasecmp(addr, bdaddr)) {
			ret = conn_info->handle;
			goto out;
		}
	}

	ret = 0;

out:
	free(conn_list);
	return ret;
}

/* Request that the ACL link to a given Bluetooth connection be high priority,
 * for improved coexistance support
 */
int vendor_set_high_priority(char *ba, uint8_t priority, uint8_t direction)
{
	int ret;
	int fd = get_hci_sock();
	int acl_handle;

	if (fd < 0)
		return fd;

	acl_handle = get_acl_handle(fd, ba);
	if (acl_handle <= 0) {
		ret = acl_handle;
		goto out;
	}

	ret = vendor_high_priority(fd, acl_handle, priority, direction);
	if (ret < 0)
		goto out;
	ret = write_flush_timeout(fd, acl_handle, 200);

out:
	close(fd);

	return ret;
}

void bt_test_pbap_get_vcf(char *data)
{
	char *addr;
	if (data == NULL)
		addr = bt_content.connected_a2dp_addr;
	else
		addr = data;

	rk_bt_pbap_get_vcf(addr, "pb", "/data/pb.vcf");
	rk_bt_pbap_get_vcf(addr, "ich", "/data/ich.vcf");
	rk_bt_pbap_get_vcf(addr, "och", "/data/och.vcf");
	rk_bt_pbap_get_vcf(addr, "mch", "/data/mch.vcf");
	rk_bt_pbap_get_vcf(addr, "cch", "/data/cch.vcf");
}

void bt_test_opp_send(char *data)
{
	char *addr, *send_file;

	if (data == NULL) {
		addr = strtok(data, " ");
		if (addr)
			send_file = strtok(NULL, " ");
	} else {
		addr = bt_content.connected_a2dp_addr;
	}

	printf("addr: %s, send_file: %s\n", addr, send_file);

	rk_bt_opp_send(addr, send_file);
}

/* at evt callback
 * 
 * +CIEV: <event>,<value>
 *																	 iPhone			Android
 * no_calls_active; 		//No calls (held or active)				 +CIEV: 2,0		+CIEV: 1,0
 * call_present_active; 	//Call is present (active or held)		 +CIEV: 2,1		+CIEV: 1,1
 * no_call_progress; 		//No call setup in progress				 +CIEV: 3,0		+CIEV: 2,0
 * incoming_call_progress; 	//Incoming call setup in progress		 +CIEV: 3,1		+CIEV: 2,1
 * outgoing_call_dialing; 	//Outgoing call setup in dialing state	 +CIEV: 3,2		+CIEV: 2,2
 * outgoing_call_alerting; 	//Outgoing call setup in alerting state	 +CIEV: 3,3		+CIEV: 2,3
 */
void at_evt_callback(char *at_evt)
{
	if (at_evt)
		printf("[AT EVT]: %s\n", at_evt);
}

void bt_test_rfcomm_open(char *data)
{
	char *addr;

	if (data == NULL)
		addr = bt_content.connected_a2dp_addr;
	else
		addr = data;

	rk_bt_rfcomm_open(addr, at_evt_callback);
}

void bt_test_rfcomm_close(char *data)
{
	rk_bt_rfcomm_close();
}

/* 
 * AT CMD:
 * pickup: ATA
 * hangup: AT+CHUP
 * redial: AT+BLDN
 * dial_num: ATD18812345678;  //注意: 拨号要在最后加分号“;”
 * volume: AT+VGS=[0-6]
 */
void bt_test_rfcomm_send(char *data)
{
	printf("data: %p\n", data);

	if (data == NULL)
		return;

	printf("data: %s\n", data);

	rk_bt_rfcomm_send(data);
}

void bt_test_adapter_connect(char *data)
{
	char *addr;
	char *ble_addr_type;

	printf("data: %s\n", data);

	addr = strtok(data, " ");
	if (addr)
		ble_addr_type = strtok(NULL, " ");

	printf("addr: %s, ble_addr_type: %s\n", addr, ble_addr_type);

	rk_adapter_connect(addr, ble_addr_type);
}
