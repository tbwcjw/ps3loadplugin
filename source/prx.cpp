#include "stdafx.h"

#include <stdio.h>
#include <unistd.h>
#include <cellstatus.h>
#include <sys/prx.h>
#include <sys/ppu_thread.h>
#include <sys/timer.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cell/cell_fs.h>

#include "miniz.h"
#include "allocator.h"
#include "vshtask.h"
#include "stdc.h"
#include "paf.h"
#include "vshmain.h"

SYS_MODULE_INFO( ps3load, 0, 1, 0);
SYS_MODULE_START(ps3load_entry);
SYS_MODULE_STOP(ps3load_stop);
SYS_MODULE_EXIT(ps3load_stop);

#define NONE		-1

//syscalls
#define SC_STOP_PRX_MODULE				482
#define SC_GET_PRX_MODULE_BY_ADDRESS	461
#define SC_RING_BUZZER					392 
#define SC_PPU_THREAD_EXIT				41

//utils
#define BEEP1			{ system_call_3(SC_RING_BUZZER, 0x1004, 0x4, 0x6); }
#define MIN(a,b)		((a)<(b) ? (a) : (b))
#define IS_ON_XMB		(GetCooperationMode() == 0)

//thread
#define IDLE_POLL		(10*100)
#define STACK_SIZE		(256*1024)
#define EXIT_PRIO		0x000	

//webman
#define WM_ADDR			0x7F000001	//127.0.0.1
#define WM_PORT			80
#define WM_TOUT_S		3			//send_wm_request timeout (seconds)
#define WM_TOUT_US		0			//send_wm_request timeout (microseconds)

//identity
#define PS3LOADTITLE    "PS3LoadPlugin "
#define	PS3LOADAUTHOR   "github:tbwcjw/ps3loadplugin"
#define PS3LOADVERSION	"V0.5"

//ps3load
#define PORT			4299
#define HAXX_MAGIC		0x48415858
#define ZIP_PATH		"/dev_hdd0/tmp/ps3load"
#define SELF_PATH		ZIP_PATH ".self"

sys_ppu_thread_t worker_thread_id = (uint64_t)NONE;
static int listen_socket = (int)NONE;
static volatile bool running = false;


static inline void _sys_ppu_thread_exit(uint64_t val)
{
	system_call_1(SC_PPU_THREAD_EXIT, val);
}

static inline sys_prx_id_t prx_get_module_id_by_address(void *addr)
{
	system_call_1(SC_GET_PRX_MODULE_BY_ADDRESS, (uint64_t)(uint32_t)addr);
	return (sys_prx_id_t)p1;
}

static void sys_prx_stop_prx_module(sys_prx_id_t prx, uint64_t meminfo)
{
	system_call_3(SC_STOP_PRX_MODULE, prx, 0, meminfo);
}

static void wait_for_xmb(int timeout_s)
{
	uint8_t t = 0;
	while (running && (View_Find("explore_plugin") == 0))
	{
		if (++t > timeout_s)
		{
			break;
		}
		sys_timer_sleep(1);
	}
}

static void sclose(int *socket_e)
{
	if (!socket_e)
	{
		return;
	}

	if (*socket_e != -1)
	{
		int s = *socket_e;
		*socket_e = -1;

		shutdown(s, SHUT_RDWR);
		socketclose(s);
		
	}
}

static int connect_to_webman()
{
	struct sockaddr_in sin;
	int s;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = WM_ADDR;
	sin.sin_port = htons(WM_PORT);
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
	{
		return -1;
	}

	if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) < 0)
	{
		sclose(&s);
		return -1;
	}

	return s;
}

static int send_wm_request(const char *cmd)
{
	int wm_s = -1;
	wm_s = connect_to_webman();
	if (wm_s < 0)
	{
		return -1;
	}

	struct timeval tv;
	tv.tv_usec = WM_TOUT_US;
	tv.tv_sec = WM_TOUT_S;
	setsockopt(wm_s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	setsockopt(wm_s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (wm_s >= 0)
	{
		char wm_cmd[1048];
		int cmd_len = snprintf(wm_cmd, sizeof(wm_cmd), "GET %s HTTP/1.0\r\n", cmd);
		
		if (cmd_len < 0 || cmd_len >= sizeof(wm_cmd))
		{
			sclose(&wm_s);
			return -1; 
		}

		send(wm_s, wm_cmd, cmd_len, 0);
		sclose(&wm_s);
		return 0;
	}
	return -1;
}

static int wait_for_webman(int timeout_s)
{
	int t = 0;

	while (running)
	{
		if (send_wm_request("/") >= 0)
		{
			return 0;
		}

		if (++t >= timeout_s)
		{
			break;
		}

		sys_timer_sleep(1);
	}

	return -1;
}

static void empty_directory(const char *path)
{
	int fd;
	CellFsDirent entry;
	uint64_t nread;

	if (cellFsOpendir(path, &fd) != CELL_FS_SUCCEEDED)
		return;

	while (1)
	{
		memset(&entry, 0, sizeof(entry));
		nread = 0;

		if (cellFsReaddir(fd, &entry, &nread) != CELL_FS_SUCCEEDED || nread == 0)
		{
			break;
		}
			

		if (!strcmp(entry.d_name, ".") || !strcmp(entry.d_name, ".."))
		{
			continue;
		}

		char newpath[0x440];

		int len = snprintf(newpath, sizeof(newpath), "%s/%s", path, entry.d_name);

		if (len < 0 || len >= sizeof(newpath))
		{
			continue;
		}
			
		CellFsStat stat;

		if (cellFsStat(newpath, &stat) != CELL_FS_SUCCEEDED)
		{
			continue;
		}
			

		if ((stat.st_mode & CELL_FS_S_IFMT) == CELL_FS_S_IFDIR)
		{
			empty_directory(newpath);
			cellFsRmdir(newpath);
		}
		else
		{
			cellFsUnlink(newpath);
		}
	}

	cellFsClosedir(fd);
}




static void worker_thread(uint64_t arg)
{
	wait_for_xmb(30);
	
	//check our connection to webman.
	if (wait_for_webman(30) < 0)
	{
		vshtask_notify("webMAN MOD not found, exiting.");
		running = false;
		goto done;
	}

	vshtask_notify(PS3LOADTITLE PS3LOADVERSION "\r\n" PS3LOADAUTHOR);

retry:
	sys_timer_usleep(IDLE_POLL);				//prevent busy wait
	if (!running)
	{
		goto done;
	}
	
	(void)arg;
	sclose(&listen_socket);
	int files;
	int32_t ret, i, wbsock;
	struct sockaddr_in server;


	mkdir(ZIP_PATH, 0777);
	empty_directory(ZIP_PATH);

	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	server.sin_port = htons(PORT);

	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_socket < 0)
	{
		vshtask_notify("Listen socket failed to open");
		goto retry;
	}

	ret = bind(listen_socket, (struct sockaddr*)&server, sizeof(struct sockaddr_in));
	if (ret < 0)
	{
		vshtask_notify("Listen socket failed to bind");
		goto retry;
	}

	ret = listen(listen_socket, 10);
	if(ret < 0) 
	{
		vshtask_notify("Listen socket failed to listen");
		goto retry;
	}

loop:
	while (running)
	{
		if (!running)
		{
			break;
		}

		sys_timer_usleep(IDLE_POLL);				//prevent busy wait

		int f = accept(listen_socket, NULL, NULL);

		if (f < 0)
		{
			if (!running)
			{
				break;
			}
			vshtask_notify("Accept failed, recreating socket");
			sclose(&listen_socket);
			goto retry;
		}

		if (!running)
		{
			sclose(&f);
			break;
		}

		uint32_t magic = 0;

		ret = recv(f, &magic, sizeof(magic), MSG_WAITALL);
		if (ret != sizeof(magic))
		{
			vshtask_notify("No magic");
			sclose(&f);
			goto loop;
		}

		vshtask_notify("ps3load magic received!");

		if (magic != HAXX_MAGIC)
		{
			vshtask_notify("Invalid magic");
			sclose(&f);
			goto loop;
		}
		
		ret = recv(f, &magic, sizeof(uint32_t), MSG_WAITALL);
		if (ret != sizeof(magic))
		{
			vshtask_notify("No magic");
			sclose(&f);
			goto loop;
		}

		uint32_t filesize = 0;
		uint32_t uncompressed = 0;
		
		ret = recv(f, &filesize, sizeof(uint32_t), MSG_WAITALL);
		if (ret != sizeof(filesize))
		{
			vshtask_notify("Invalid filesize");
			sclose(&f);
			goto loop;
		}

		ret = recv(f, &uncompressed, sizeof(uint32_t), MSG_WAITALL);
		if (ret != sizeof(uncompressed))
		{
			vshtask_notify("No uncompressed");
			sclose(&f);
			goto loop;
		}

		vshtask_notify("Receiving data...");

		uint32_t cnt;
		uint32_t pos = 0;
		uint8_t *data = (uint8_t*)malloc(filesize);

		if (!data)
		{
			vshtask_notify("Failed to allocate receive buffer");
			sclose(&f);
			goto loop;
		}

		while (pos < filesize)
		{
			cnt = MIN(4096, filesize - pos);
			ret = recv(f, data + pos, cnt, MSG_WAITALL);
			if (ret <= 0)
			{
				free(data);
				sclose(&f);
				goto loop;
			}

			pos += ret;
		}

		sclose(&f);

		vshtask_notify("Decompressing...");

		if (uncompressed)
		{
			uint8_t *compressed = data;
			uLongf final = uncompressed;

			data = (uint8_t*)malloc(final);

			if (!data)
			{
				free(compressed);
				vshtask_notify("Failed to allocate decompression buffer");
				goto loop;
			}

			ret = uncompress(data, &final, compressed, filesize);
			if (ret != Z_OK)
			{
				free(compressed);
				free(data);
				vshtask_notify("Decompression failed");
				goto loop;
			}

			free(compressed);

			if (uncompressed != final)
			{
				free(data);
				continue;
			}

			uncompressed = final;
		}
		else
		{
			uncompressed = filesize;
		}

		f = open(SELF_PATH, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
		if (f < 0)
		{
			vshtask_notify("Failed to open");
			free(data);
			goto loop;
		}

		pos = 0;
		while (pos < uncompressed)
		{
			cnt = MIN(4096, uncompressed - pos);
			if (write(f, data + pos, cnt) != cnt)
			{
				vshtask_notify("Failed to write");
				close(f);
				free(data);
				goto loop;
			}

			pos += cnt;
		}
		close(f);
		

		char bootpath[CELL_FS_MAX_FS_PATH_LENGTH + 1];

		//50 4B 03 04
		bool is_zip = (uncompressed >= 4 && data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 && data[3] == 0x04);
		
		if (!is_zip)
		{
			strcpy(bootpath, SELF_PATH);
			goto launch;
		}

		mz_zip_archive archive;
		memset(&archive, 0, sizeof(archive));

		if (!mz_zip_reader_init_mem(&archive, data, uncompressed, 0))
		{
			vshtask_notify("Failed to initalize zip reader");
			free(data);
			goto loop;
		}

		files = (int)mz_zip_reader_get_num_files(&archive);
		strcpy(bootpath, SELF_PATH);

		if (files > 0)
		{
			strcpy(bootpath, "");

			for (i = 0; i < files; i++)
			{
				char path[CELL_FS_MAX_FS_PATH_LENGTH + 1];
				char filename[MZ_ZIP_MAX_ARCHIVE_FILENAME_SIZE];
				mz_zip_reader_get_filename(&archive, i, filename, sizeof(filename));

				strcpy(path, ZIP_PATH);

				if (!filename)
				{
					continue;
				}
				if (filename[0] != '/')
				{
					strcat(path, "/");
				}
				strcat(path, filename);

#define ENDS_WITH(needle) (strlen(filename)>=strlen(needle) && !strcasecmp(filename + strlen(filename) - strlen(needle), needle))
				
				if (ENDS_WITH("EBOOT.BIN") || ENDS_WITH(".self"))
				{
					strcpy(bootpath, path);
				}

				if (filename[strlen(filename) - 1] != '/')
				{
					mz_zip_archive_file_stat st;
					
					if (!mz_zip_reader_file_stat(&archive, i, &st))
					{
						vshtask_notify("Unable to read file in zip");
						continue;
					}
					
					size_t size = 0;
					void *zip_buffer = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);

					if (!zip_buffer)
					{
						vshtask_notify("Unable to extract file from zip");
						continue;
					}

					f = open(path, (CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY), (S_IRWXU | S_IRWXG | S_IRWXO));
					if (f < 0)
					{
						mz_free(zip_buffer);
						mz_zip_reader_end(&archive);
						free(data);
						goto loop;
					}

					pos = 0;

					uint8_t *buffer = (uint8_t *)zip_buffer;

					while (pos < size)
					{
						cnt = MIN(4096, size - pos);

						if (write(f, buffer + pos, cnt) != cnt)
						{
							close(f);
							mz_free(zip_buffer);
							mz_zip_reader_end(&archive);
							free(data);
							goto loop;
						}

						pos += cnt;
					}

					mz_free(zip_buffer);
					close(f);
				}
				else
					mkdir(path, 0777);
			}
		}
		mz_zip_reader_end(&archive);
launch:

		sys_timer_usleep(IDLE_POLL);
		//exit to xmb
		if (!IS_ON_XMB)
		{
			wbsock = send_wm_request("/xmb.ps3$exit");
			if (wbsock < 0)
			{
				vshtask_notify("Waiting for process exit...");
			}
			wait_for_xmb(10);
		}

		//unmount and reset app home
		wbsock = send_wm_request("/mount.ps3/unmount");
		if (wbsock < 0)
		{
			vshtask_notify("Failed to unmount emulated bdvd");
		}
		wbsock = send_wm_request("/app_home.ps3");
		if (wbsock < 0)
		{
			vshtask_notify("Failed to reset app_home");
		}


		free(data);
		data = NULL;

		if (!strlen(bootpath)) continue;

		//mount
		char cmd[1048];
		snprintf(cmd, sizeof(cmd), "/mount.ps3%s", bootpath);
		vshtask_notify(bootpath);

mount:

		if (!running)
		{
			goto done;
		}

		wbsock = send_wm_request(cmd);
		if (wbsock < 0)
		{
			vshtask_notify("Failed to connect to webMAN");
			goto launch;
		}

		sys_timer_sleep(5);	 //don't remove this or him fukkin die
		if (IS_ON_XMB)
		{
			goto mount;
		}

	}

done:
	sclose(&listen_socket);
	sys_ppu_thread_exit(0);
}

static void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address((void*)finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	sys_prx_stop_prx_module(prx, (uint64_t)(uint32_t)meminfo);
}

extern "C" int ps3load_entry(void)
{
	if (running)
	{
		return SYS_PRX_RESIDENT;
	}

	running = true;
	sys_ppu_thread_create(&worker_thread_id, worker_thread, 0, 1000, STACK_SIZE, SYS_PPU_THREAD_CREATE_JOINABLE, "ps3load");

	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}

static void exit_thread(uint64_t arg)
{
	running = false;
	uint64_t ret = 0;

	if (listen_socket != (int)NONE)
	{
		sclose(&listen_socket);
	}

	if (worker_thread_id != (uint64_t)NONE)
	{
		sys_ppu_thread_join(worker_thread_id, &ret);
		worker_thread_id = (uint64_t)NONE;
	}
	sys_ppu_thread_exit(0);

}

extern "C" int ps3load_stop(void)
{
	uint64_t ret = 0;
	sys_ppu_thread_t exit_thread_id;

	sys_ppu_thread_create(&exit_thread_id, exit_thread, NULL, EXIT_PRIO, STACK_SIZE, SYS_PPU_THREAD_CREATE_JOINABLE, "ps3load_exit");
	sys_ppu_thread_join(exit_thread_id, &ret);

	finalize_module();

	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}