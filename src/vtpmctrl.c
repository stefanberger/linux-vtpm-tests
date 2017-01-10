/*
 * vtpmctrl.c -- Linux vTPM driver control program
 *
 * (c) Copyright IBM Corporation 2015.
 *
 * Author: Stefan Berger <stefanb@us.ibm.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the names of the IBM Corporation nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <endian.h>
#include <stdint.h>
#include <stdbool.h>

#include <linux/vtpm_proxy.h>

#define TPM_ORD_STARTUP        0x00000099
#define TPM_ORD_GETCAPABILITY  0x00000065
#define TPM_ORD_CONTINUESELFTEST 0x00000053
#define TPM_ORD_PCRREAD        0x00000015

#define TPM2_CC_STARTUP        0x00000144
#define TPM2_CC_GET_CAPABILITY 0x0000017A

void dump_buffer(const unsigned char *buffer, int len)
{
	int i = 0;

	while (i < len) {
		printf("0x%02x ", buffer[i]);
		i++;
		if (i % 16 == 0)
			printf("\n");
	}
	printf("\n");
}

int spawn_device(struct vtpm_proxy_new_dev *vtpm_new_dev)
{
	int fd, n;

	fd = open("/dev/vtpmx", O_RDWR);
	if (fd < 0) {
		perror("Could not open /dev/vtpmx");
		return 1;
	}

	n = ioctl(fd, VTPM_PROXY_IOC_NEW_DEV, vtpm_new_dev);
	if (n != 0) {
		perror("ioctl to create new device failed");
		close(fd);
		return 1;
	}

	close(fd);

	return 0;
}

int vtpmctrl_create(bool exit_on_user_request, bool is_tpm2)
{
	int fd, n, option, li, serverfd, nn;
	struct vtpm_proxy_new_dev vtpm_new_dev = {
		.flags = 0,
	};
	char tpmdev[16];
	unsigned char buffer[4096];
	const unsigned char *bufferp;
	size_t bufferlen;
	const unsigned char tpm_success_resp[] = {
		0x00, 0xc4, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00
	};
	const unsigned char tpm2_success_resp[] = {
		0x80, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00
	};
	const unsigned char timeout_req[] = {
		0x00, 0xc1, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x65,
		0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
		0x01, 0x15
	};
	const unsigned char timeout_res[] = {
		0x00, 0xc4, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x01, 0x00, 0x00,
		0x00, 0x02, 0x00, 0x00,
		0x00, 0x03, 0x00, 0x00,
		0x00, 0x04, 0x00, 0x00,
	};
	const unsigned char duration_req[] = {
		0x00, 0xc1, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x65,
		0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00,
		0x01, 0x20
	};
	const unsigned char duration_res[] = {
		0x00, 0xc4, 0x00, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x01, 0x00, 0x00,
		0x00, 0x02, 0x00, 0x00,
		0x00, 0x03, 0x00, 0x00,
	};
	const unsigned char tpm_read_pcr_resp[] = {
		0x00, 0xc4, 0x00, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00,
                0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11
	};
	const unsigned char tpm2_pt_tot_commands[] = {
		0x80, 0x01, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x01, 0x7a,
		0x00, 0x00, 0x00, 0x06,
		0x00, 0x00, 0x01, 0x29,
		0x00, 0x00, 0x00, 0x01
	};
	const unsigned char tpm2_pt_tot_commands_resp[] = {
		0x80, 0x01, 0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x00,
		0x00,
		0x00, 0x00, 0x00, 0x06,
		0x00, 0x00, 0x00, 0x01,
		0x00, 0x00, 0x01, 0x29,
		0x00, 0x00, 0x00, 0x40  /* number of commands */
	};
	const unsigned char tpm2_pt_cc_first[] = {
	        0x80, 0x01, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x01, 0x7a,
	        0x00, 0x00, 0x00, 0x02,
	        0x00, 0x00, 0x01, 0x1f,
	        0x00, 0x00, 0x00, 0x40  /* number of commands */
        };
	const unsigned char tpm2_pt_cc_first_resp[] = {
	        0x80, 0x01, 0x00, 0x00, 0x01, 0x13, 0x00, 0x00, 0x00, 0x00,
		0x00,
		0x00, 0x00, 0x00, 0x06,
	        0x00, 0x00, 0x00, 0x40, /* number of commands */
		0x04, 0x40, 0x01, 0x1f,
		0x04, 0x40, 0x01, 0x20,
		0x02, 0xc0, 0x01, 0x21,
		0x04, 0x40, 0x01, 0x22,
		0x02, 0xc0, 0x01, 0x24,
		0x02, 0xc0, 0x01, 0x25,
		0x02, 0xc0, 0x01, 0x26,
		0x02, 0x40, 0x01, 0x27,
		0x02, 0x40, 0x01, 0x28,
		0x02, 0x40, 0x01, 0x29,
		0x02, 0x40, 0x01, 0x2a,
		0x02, 0x40, 0x01, 0x2b,
		0x02, 0x40, 0x01, 0x2c,
		0x02, 0x40, 0x01, 0x2d,
		0x02, 0x40, 0x01, 0x2e,
		0x02, 0x00, 0x01, 0x30,
		0x12, 0x00, 0x01, 0x31,
		0x02, 0x00, 0x01, 0x32,
		0x04, 0x40, 0x01, 0x33,
		0x04, 0x40, 0x01, 0x34,
		0x04, 0x40, 0x01, 0x35,
		0x04, 0x40, 0x01, 0x36,
		0x04, 0x40, 0x01, 0x37,
		0x04, 0x40, 0x01, 0x38,
		0x02, 0x40, 0x01, 0x39,
		0x02, 0x40, 0x01, 0x3a,
		0x02, 0x40, 0x01, 0x3b,
		0x02, 0x40, 0x01, 0x3c,
		0x02, 0x40, 0x01, 0x3d,
		0x03, 0x00, 0x01, 0x3e,
		0x02, 0x40, 0x01, 0x3f,
		0x02, 0x40, 0x01, 0x40,
		0x00, 0x40, 0x01, 0x42,
		0x00, 0x40, 0x01, 0x43,
		0x00, 0x40, 0x01, 0x44,
		0x00, 0x40, 0x01, 0x45,
		0x00, 0x40, 0x01, 0x46,
		0x04, 0x00, 0x01, 0x47,
		0x04, 0x00, 0x01, 0x48,
		0x06, 0x00, 0x01, 0x49,
		0x04, 0x00, 0x01, 0x4a,
		0x04, 0x00, 0x01, 0x4b,
		0x04, 0x00, 0x01, 0x4c,
		0x06, 0x00, 0x01, 0x4d,
		0x04, 0x00, 0x01, 0x4e,
		0x04, 0x00, 0x01, 0x4f,
		0x04, 0x00, 0x01, 0x50,
		0x04, 0x00, 0x01, 0x51,
		0x04, 0x00, 0x01, 0x52,
		0x02, 0x00, 0x01, 0x53,
		0x02, 0x00, 0x01, 0x54,
		0x02, 0x00, 0x01, 0x55,
		0x02, 0x00, 0x01, 0x56,
		0x12, 0x00, 0x01, 0x57,
		0x02, 0x00, 0x01, 0x58,
		0x02, 0x00, 0x01, 0x59,
		0x12, 0x00, 0x01, 0x5b,
		0x02, 0x00, 0x01, 0x5c,
		0x02, 0x00, 0x01, 0x5d,
		0x02, 0x00, 0x01, 0x5e,
		0x04, 0x00, 0x01, 0x60,
		0x10, 0x00, 0x01, 0x61,
		0x02, 0x00, 0x01, 0x62,
		0x02, 0x00, 0x01, 0x63,
        };
	uint32_t ordinal;
	bool started = false;

	setvbuf(stdout, 0, _IONBF, 0);

	if (is_tpm2)
		vtpm_new_dev.flags |= VTPM_PROXY_FLAG_TPM2;

	if (spawn_device(&vtpm_new_dev))
		return 1;

	snprintf(tpmdev, sizeof(tpmdev), "/dev/tpm%u",
		 vtpm_new_dev.tpm_num);

	serverfd = vtpm_new_dev.fd;

	printf("Created TPM device %s; vTPM device has fd %d, "
	       "major/minor = %u/%u.\n",
	       tpmdev, serverfd, vtpm_new_dev.major, vtpm_new_dev.minor);

	while (1) {
		n = read(serverfd, buffer, sizeof(buffer));
		if (n > 0) {
			printf("Request with %d bytes:\n", n);
			dump_buffer(buffer, n);

			ordinal = be32toh(*(uint32_t *)&(buffer[6]));

			/* the kernel sends a variety of commands; so we won't exit on 
			   ContinueSelfTest or PcrRead */
			if (started && exit_on_user_request &&
			    ordinal != TPM_ORD_CONTINUESELFTEST &&
			    ordinal != TPM_ORD_PCRREAD &&
			    ordinal != TPM2_CC_GET_CAPABILITY) {
				printf("Exiting upon user sending a request\n");
				return 0;
			}

			switch (ordinal) {
			case TPM_ORD_STARTUP:
			        bufferp = tpm_success_resp;
			        bufferlen = sizeof(tpm_success_resp);
				n = write(serverfd, tpm_success_resp, sizeof(tpm_success_resp));
				break;
			case TPM2_CC_STARTUP:
			        bufferp = tpm2_success_resp;
			        bufferlen = sizeof(tpm2_success_resp);
				started = true;
				break;
			case TPM_ORD_GETCAPABILITY:
				if (!memcmp(timeout_req, buffer, sizeof(timeout_req))) {
				        bufferp = timeout_res;
				        bufferlen = sizeof(timeout_res);

				} else if (!memcmp(duration_req, buffer, sizeof(duration_req))) {
				        bufferp = duration_res;
				        bufferlen = sizeof(duration_res);
					started = true;
				} else {
				        bufferp = tpm_success_resp;
				        bufferlen = sizeof(tpm_success_resp);
				}
				break;
                        case TPM_ORD_CONTINUESELFTEST:
			        bufferp = tpm_success_resp;
			        bufferlen = sizeof(tpm_success_resp);
                                break;
                        case TPM_ORD_PCRREAD:
			        bufferp = tpm_read_pcr_resp;
			        bufferlen = sizeof(tpm_read_pcr_resp);
                                break;
                        case TPM2_CC_GET_CAPABILITY:
                                if (!memcmp(tpm2_pt_tot_commands, buffer, sizeof(tpm2_pt_tot_commands))) {
                                        bufferp = tpm2_pt_tot_commands_resp;
                                        bufferlen = sizeof(tpm2_pt_tot_commands_resp);
                                } else if (!memcmp(tpm2_pt_cc_first, buffer, sizeof(tpm2_pt_cc_first))) {
                                        bufferp = tpm2_pt_cc_first_resp;
                                        bufferlen = sizeof(tpm2_pt_cc_first_resp);
                                } else {
                                        printf("Unknown TPM2_CC_GET_CAPABILITY request\n");
                                }
                                break;
			default:
				if (buffer[0] == 0x80) {
        			        bufferp = tpm2_success_resp;
        			        bufferlen = sizeof(tpm2_success_resp);
				} else {
        			        bufferp = tpm_success_resp;
        			        bufferlen = sizeof(tpm_success_resp);
				}
				break;
			}
			n = write(serverfd, bufferp, bufferlen);
			if (n < 0) {
				printf("Error from writing the response: %s\n",
				       strerror(errno));
				break;
			} else {
				printf("Sent response with %d bytes.\n", n);
			}
			dump_buffer(bufferp, bufferlen);
		} else {
		        printf("Did not receive data from read() ; n=%d ", n);
		        if (n < 0) {
		                printf(": %s\n", strerror(errno));
		        }
			break;
		}
	}

	printf("Exiting loop.\n");

	return 0;
}

void vtpmctrl_spawn(int argc, char *argv[], char *envp[], int is_tpm2)
{
	const char *filename = argv[0];
	int i;
	char fdstr[10];
	struct vtpm_proxy_new_dev vtpm_new_dev = {
		.flags = 0,
	};

	if (argc < 1) {
		fprintf(stderr, "Missing filename.\n");
		exit(EXIT_FAILURE);
	}
	if (is_tpm2)
		vtpm_new_dev.flags |= VTPM_PROXY_FLAG_TPM2;
	if (spawn_device(&vtpm_new_dev))
		exit(EXIT_FAILURE);

	printf("Created TPM device /dev/tpm%d; vTPM device has fd %d, "
	       "major/minor = %u/%u.\n",
	       vtpm_new_dev.tpm_num, vtpm_new_dev.fd,
	       vtpm_new_dev.major, vtpm_new_dev.minor);

	snprintf(fdstr, sizeof(fdstr), "%d", vtpm_new_dev.fd);

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "%fd")) {
			argv[i] = fdstr;
		}
	}


	execve(filename, &argv[0], envp);
	fprintf(stderr, "Could not execve '%s' : %s",
		filename,
		strerror(errno));
	/* should never get here */
	exit(EXIT_FAILURE);
}

static void usage(const  char *prg)
{
        printf(
"This is a test tool for the Linux vTPM proxy driver\n"
"\n"
"Usage: %s options\n"
"\n"
"The following options are supported:\n"
""
"--tpm2                 : A TPM 2 is used\n"
"\n"
"--exit-on-user-request : The tool exits upon a request issued by the user.\n"
"\n"
"--spawn program prg-params...\n"
"                       : Spawn the given program and pass to it the\n"
"                         file descriptor on which to listen for TPM\n"
"                         commands. If a parameter '%%fd' appears among\n"
"                         the prg-params passed to the program, it will\n"
"                         be replaced by that file descriptor number.\n"
"\n"
"                         This option must be the last option passed.\n"
"\n"
"--help|-h|-?           : Display this help screen and exit.\n"
"\n"
"Examples:\n"
"%s --tpm2 \\\n"
"  --spawn /bin/swtpm chardev --tpm2 --fd %%fd --tpmstate dir=/tmp\n"
"\n"
"%s \\\n"
"  --spawn /bin/swtpm chardev --fd %%fd --tpmstate dir=/tmp\n"
"\n",
prg, prg, prg, prg
);
}

int main(int argc, char *argv[], char *envp[])
{
	bool exit_on_user_request = false;
	bool is_tpm2 = false;
	int idx = 1;

	while (idx < argc) {
		if (!strcmp(argv[idx], "--exit-on-user-request")) {
			exit_on_user_request = true;
		} else if (!strcmp(argv[idx], "--tpm2")) {
			is_tpm2 = true;
		} else if (!strcmp(argv[idx], "--spawn")) {
			vtpmctrl_spawn(argc-1-idx, &argv[1+idx], envp, is_tpm2);
		} else if (!strcmp(argv[idx], "--help") ||
		           !strcmp(argv[idx], "-h") ||
		           !strcmp(argv[idx], "-?")) {
		        usage(argv[0]);
		        exit(EXIT_SUCCESS);
		} else {
		        fprintf(stderr, "Unkown option.\n");
		        usage(argv[0]);
		        exit(EXIT_FAILURE);
		}
		idx++;
	}

	return vtpmctrl_create(exit_on_user_request, is_tpm2);
}
