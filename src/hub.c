/*
 * uhub - A tiny ADC p2p connection hub
 * Copyright (C) 2007-2009, Jan Vidar Krey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "uhub.h"

int hub_handle_message(struct user* u, const char* line, size_t length)
{
	int ret = 0;
	struct adc_message* cmd = 0;
	
#ifdef NETWORK_DUMP_DEBUG
	hub_log(log_protocol, "recv %s: %s", sid_to_string(u->id.sid), line);
#endif
	
	if (user_is_disconnecting(u))
		return -1;
	
	cmd = adc_msg_parse_verify(u, line, length);
	if (cmd)
	{
		switch (cmd->cmd)
		{
			case ADC_CMD_HSUP: ret = hub_handle_support(u, cmd); break;
			case ADC_CMD_HPAS: ret = hub_handle_password(u, cmd); break;
			case ADC_CMD_BINF: ret = hub_handle_info(u, cmd); break;

#ifdef ADC_UDP_OPERATION
			case ADC_CMD_HCHK: ret = hub_handle_autocheck(u, cmd); break;
#endif
			case ADC_CMD_DINF:
			case ADC_CMD_EINF:
			case ADC_CMD_FINF:
				/* these must never be allowed for security reasons,
				   so we ignore them. */
				break;

			case ADC_CMD_EMSG:
			case ADC_CMD_DMSG:
			case ADC_CMD_BMSG:
			case ADC_CMD_FMSG:
				ret = hub_handle_chat_message(u, cmd);
				break;

			case ADC_CMD_BSCH:
			case ADC_CMD_DSCH:
			case ADC_CMD_ESCH:
			case ADC_CMD_FSCH:
			case ADC_CMD_DRES:
			case ADC_CMD_DRCM:
			case ADC_CMD_DCTM:
				cmd->priority = -1;
				if (u->hub->config->chat_only && u->credentials < cred_operator)
				{
					/* These below aren't allowed in chat only hubs */
					break;
				}
			
			default:
				if (user_is_logged_in(u))
				{
					ret = route_message(u, cmd);
				}
				else
				{
					ret = -1;
				}
				break;
		}
		adc_msg_free(cmd);
	}
	else
	{
		if (!user_is_logged_in(u))
		{
			ret = -1;
		}
	}
	
	return ret;
}


int hub_handle_support(struct user* u, struct adc_message* cmd)
{
	int ret = 0;
	int index = 0;
	int ok = 1;
	char* arg = adc_msg_get_argument(cmd, index);
	struct timeval timeout = { TIMEOUT_HANDSHAKE, 0 };

	if (u->hub->status == hub_status_disabled && u->state == state_protocol)
	{
		on_login_failure(u, status_msg_hub_disabled);
		return -1;
	}
	
	while (arg)
	{
	
		if (strlen(arg) == 6)
		{
			fourcc_t fourcc = FOURCC(arg[2], arg[3], arg[4], arg[5]);
			if (strncmp(arg, ADC_SUP_FLAG_ADD, 2) == 0)
			{
				user_support_add(u, fourcc);
			}
			else if (strncmp(arg, ADC_SUP_FLAG_REMOVE, 2) == 0)
			{
				user_support_remove(u, fourcc);
			}
			else
			{
				ok = 0;
			}
		}
		else
		{
			ok = 0;
		}
		
		index++;
		hub_free(arg);
		arg = adc_msg_get_argument(cmd, index);
	}
	
	if (u->state == state_protocol)
	{
		if (index == 0) ok = 0; /* Need to support *SOMETHING*, at least BASE */
	
		if (ok)
		{
			hub_send_handshake(u);
			if (u->ev_read)
				event_add(u->ev_read, &timeout);
		}
		else
		{
			/* disconnect user. Do not send crap during initial handshake! */
			user_disconnect(u, quit_logon_error);
			ret = -1;
		}
	}
	
	return ret;
}


int hub_handle_password(struct user* u, struct adc_message* cmd)
{
	char* password = adc_msg_get_argument(cmd, 0);
	int ret = 0;

	if (u->state == state_verify)
	{
		if (password_verify(u, password))
		{
			on_login_success(u);
		}
		else
		{
			on_login_failure(u, status_msg_auth_invalid_password);
			ret = -1;
		}
	}
	
	hub_free(password);
	return ret;
}


int hub_handle_chat_message(struct user* u, struct adc_message* cmd)
{
	char* message = adc_msg_get_argument(cmd, 0);
	int ret = 0;
	int relay = 1;
	
	/* TODO: Check for hub-commands here. Set relay to 0 and the message will not be sent to other users. */
	if (message[0] == '!' || message[0] == '+')
	{
	    relay = command_dipatcher(u, message);
	}

	if (relay && user_is_logged_in(u))
	{
		/* adc_msg_remove_named_argument(cmd, "PM"); */
		ret = route_message(u, cmd);
	}
	
	free(message);
	return ret;
}

int on_kick(struct user* u, struct adc_message* cmd)
{
	hub_log(log_error, "on_kick() not implemented");
	return -1;
}

#ifdef ADC_UDP_OPERATION
int hub_handle_autocheck(struct user* u, struct adc_message* cmd)
{
	char* port_str = adc_msg_get_argument(cmd, 0);
	char* token    = adc_msg_get_argument(cmd, 1);
	int port = 0;
	
	if (!port_str || !token || strlen(token) != 4)
	{
		hub_free(port_str);
		hub_free(token);
		return -1;
	}
	
	port = uhub_atoi(port_str);
	
	if (port == 0 || port > 65535)
	{
		hub_free(port_str);
		hub_free(token);
		return -1;
	}
	
	hub_send_autocheck(u, port, token);
	
	hub_free(port_str);
	hub_free(token);
	
	return 0;
}
#endif


void hub_send_autocheck(struct user* u, uint16_t port, const char* token)
{
	
}


void hub_send_support(struct user* u)
{
	if (user_is_connecting(u) || user_is_logged_in(u))
	{
		route_to_user(u, u->hub->command_support);
	}
}


void hub_send_sid(struct user* u)
{
	struct adc_message* command;
	if (user_is_connecting(u))
	{
		command = adc_msg_construct(ADC_CMD_ISID, 10);
		u->id.sid = user_manager_get_free_sid(u->hub);
		adc_msg_add_argument(command, (const char*) sid_to_string(u->id.sid));
		route_to_user(u, command);
		adc_msg_free(command);
	}
}


void hub_send_ping(struct user* user)
{
	/* This will just send a newline, despite appearing to do more below. */
	struct adc_message* ping = adc_msg_construct(0, 0);
	ping->cache[0]     = '\n';
	ping->cache[1]     = 0;
	ping->length       = 1;
	ping->priority     = 1;
	route_to_user(user, ping);
	adc_msg_free(ping);
}


void hub_send_hubinfo(struct user* u)
{
	struct adc_message* info = adc_msg_copy(u->hub->command_info);
	int value = 0;
	
	if (user_flag_get(u, feature_ping))
	{
/*
		FIXME: These are missing:
		HH - Hub Host address ( DNS or IP )
		WS - Hub Website
		NE - Hub Network
		OW - Hub Owner name
*/
		adc_msg_add_named_argument(info, "UC", uhub_itoa(hub_get_user_count(u->hub)));
		adc_msg_add_named_argument(info, "MC", uhub_itoa(hub_get_max_user_count(u->hub)));
		adc_msg_add_named_argument(info, "SS", uhub_ulltoa(hub_get_shared_size(u->hub)));
		adc_msg_add_named_argument(info, "SF", uhub_itoa(hub_get_shared_files(u->hub)));
		
		/* Maximum/minimum share size */
		value = hub_get_max_share(u->hub);
		if (value) adc_msg_add_named_argument(info, "XS", uhub_itoa(value));
		value = hub_get_min_share(u->hub);
		if (value) adc_msg_add_named_argument(info, "MS", uhub_itoa(value));
		
		/* Maximum/minimum upload slots allowed per user */
		value = hub_get_max_slots(u->hub);
		if (value) adc_msg_add_named_argument(info, "XL", uhub_itoa(value));
		value = hub_get_min_slots(u->hub);
		if (value) adc_msg_add_named_argument(info, "ML", uhub_itoa(value));
		
		/* guest users must be on min/max hubs */
		value = hub_get_max_hubs_user(u->hub);
		if (value) adc_msg_add_named_argument(info, "XU", uhub_itoa(value));
		value = hub_get_min_hubs_user(u->hub);
		if (value) adc_msg_add_named_argument(info, "MU", uhub_itoa(value));
		
		/* registered users must be on min/max hubs */
		value = hub_get_max_hubs_reg(u->hub);
		if (value) adc_msg_add_named_argument(info, "XR", uhub_itoa(value));
		value = hub_get_min_hubs_reg(u->hub);
		if (value) adc_msg_add_named_argument(info, "MR", uhub_itoa(value));
		
		/* operators must be on min/max hubs */
		value = hub_get_max_hubs_op(u->hub);
		if (value) adc_msg_add_named_argument(info, "XO", uhub_itoa(value));
		value = hub_get_min_hubs_op(u->hub);
		if (value) adc_msg_add_named_argument(info, "MO", uhub_itoa(value));
		
		/* uptime in seconds */
		adc_msg_add_named_argument(info, "UP", uhub_itoa((int) difftime(time(0), u->hub->tm_started)));
	}
	
	if (user_is_connecting(u) || user_is_logged_in(u))
	{
		route_to_user(u, info);
	}
	adc_msg_free(info);
	
	/* Only send banner when connecting */
	if (u->hub->config->show_banner && user_is_connecting(u))
	{
		route_to_user(u, u->hub->command_banner);
	}
	
	
}


void hub_send_handshake(struct user* u)
{
	hub_send_support(u);
	hub_send_sid(u);
	hub_send_hubinfo(u);

	if (!user_is_disconnecting(u))
	{
		user_set_state(u, state_identify);
	}
}


void hub_send_motd(struct user* u)
{
	if (u->hub->command_motd)
	{
		route_to_user(u, u->hub->command_motd);
	}
}


void hub_send_password_challenge(struct user* u)
{
	struct adc_message* igpa;
	igpa = adc_msg_construct(ADC_CMD_IGPA, 38);
	adc_msg_add_argument(igpa, password_generate_challenge(u));
	user_set_state(u, state_verify);
	route_to_user(u, igpa);
	adc_msg_free(igpa);
}

static void hub_event_dispatcher(void* callback_data, struct event_data* message)
{
/*
	struct hub_info* hub = (struct hub_info*) callback_data;
	hub_log(log_trace, "hub_event_dispatcher: %x (ptr=%p)", message->id, message->ptr);
*/
	
	switch (message->id)
	{
		case UHUB_EVENT_USER_JOIN:
		{
			if (user_is_disconnecting((struct user*) message->ptr))
				break;
		
			if (message->flags)
			{
				hub_send_password_challenge((struct user*) message->ptr);
			}
			else
			{
				on_login_success((struct user*) message->ptr);
			}
			break;
		}

		case UHUB_EVENT_USER_QUIT:
		{
			user_manager_remove((struct user*) message->ptr);
			send_quit_message((struct user*) message->ptr);
			on_logout_user((struct user*) message->ptr);
			user_schedule_destroy((struct user*) message->ptr);
			break;
		}
		
		case UHUB_EVENT_USER_DESTROY:
		{
			hub_log(log_trace, "hub_event_dispatcher: UHUB_EVENT_USER_DESTROY (ptr=%p)", message->ptr);
			user_destroy((struct user*) message->ptr);
			break;
		}
		
		default:
			/* FIXME: ignored */
			break;
	}
}


struct hub_info* hub_start_service(struct hub_config* config)
{
	struct hub_info* hub = 0;
	int server_tcp, ret, ipv6_supported, af;
#ifdef ADC_UDP_OPERATION
	int server_udp;
#endif
	struct sockaddr_storage addr;
	socklen_t sockaddr_size;
	char address_buf[INET6_ADDRSTRLEN+1];
	
	hub = hub_malloc_zero(sizeof(struct hub_info));
	if (!hub)
	{
		hub_log(log_fatal, "Unable to allocate memory for hub");
		return 0;
	}
	
	hub->tm_started = time(0);
	
	ipv6_supported = net_is_ipv6_supported();
	
	if (ipv6_supported)
		hub_log(log_debug, "IPv6 supported.");
	else
		hub_log(log_debug, "IPv6 not supported.");
	
	if (ip_convert_address(config->server_bind_addr, config->server_port, (struct sockaddr*) &addr, &sockaddr_size) == -1)
	{
		hub_free(hub);
		return 0;
	}
	
	af = addr.ss_family;
	if (af == AF_INET)
	{
		net_address_to_string(AF_INET, &((struct sockaddr_in*) &addr)->sin_addr, address_buf, INET6_ADDRSTRLEN);
	}
	else if (af == AF_INET6)
	{
		net_address_to_string(AF_INET6, &((struct sockaddr_in6*) &addr)->sin6_addr, address_buf, INET6_ADDRSTRLEN);
	}

	hub_log(log_info, "Starting server, listening on %s:%d...", address_buf, config->server_port);

	server_tcp = net_socket_create(af, SOCK_STREAM, IPPROTO_TCP);
	if (server_tcp == -1)
	{
		hub_free(hub);
		return 0;
	}
	
#ifdef ADC_UDP_OPERATION
	server_udp = net_socket_create(af, SOCK_DGRAM,  IPPROTO_UDP);
	if (server_udp == -1)
	{
		hub_free(hub);
		return 0;
	}
#endif
	
	ret = net_set_reuseaddress(server_tcp, 1);
	if (ret == -1)
	{
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}
	
#ifdef ADC_UDP_OPERATION
	ret = net_set_reuseaddress(server_udp, 1);
	if (ret == -1)
	{
		hub_free(hub);
		net_close(server_tcp);
		net_close(server_udp);
		return 0;
	}
#endif

	
	ret = net_set_nonblocking(server_tcp, 1);
	if (ret == -1)
	{
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}
	
#ifdef ADC_UDP_OPERATION
	ret = net_set_nonblocking(server_udp, 1);
	if (ret == -1)
	{
		hub_free(hub);
		net_close(server_tcp);
		net_close(server_udp);
		return 0;
	}
#endif
	
	
	ret = net_bind(server_tcp, (struct sockaddr*) &addr, sockaddr_size);
	if (ret == -1)
	{
		hub_log(log_fatal, "hub_start_service(): Unable to bind to TCP local address. errno=%d, str=%s", net_error(), net_error_string(net_error()));
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}

#ifdef ADC_UDP_OPERATION
	ret = net_bind(server_udp, (struct sockaddr*) &addr, sockaddr_size);
	if (ret == -1)
	{
		hub_log(log_fatal, "hub_start_service(): Unable to bind to UDP local address. errno=%d, str=%s", net_error(), net_error_string(net_error()));
		hub_free(hub);
		net_close(server_tcp);
		net_close(server_udp);
		return 0;
	}
#endif	

	ret = net_listen(server_tcp, SERVER_BACKLOG);
	if (ret == -1)
	{
		hub_log(log_fatal, "hub_start_service(): Unable to listen to socket");
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}
	
	hub->fd_tcp = server_tcp;
#ifdef ADC_UDP_OPERATION	
	hub->fd_udp = server_udp;
#endif
	hub->config = config;
	hub->users = NULL;
	
	if (user_manager_init(hub) == -1)
	{
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}
	
	event_set(&hub->ev_accept, hub->fd_tcp, EV_READ | EV_PERSIST, net_on_accept, hub);
	if (event_add(&hub->ev_accept, NULL) == -1)
	{
		user_manager_shutdown(hub);
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}

#ifdef ADC_UDP_OPERATION
	event_set(&hub->ev_datagram, hub->fd_udp, EV_READ | EV_PERSIST, net_on_packet, hub);
	if (event_add(&hub->ev_datagram, NULL) == -1)
	{
		user_manager_shutdown(hub);
		hub_free(hub);
		net_close(server_tcp);
		net_close(server_udp);
		return 0;
	}
#endif

	if (event_queue_initialize(&hub->queue, hub_event_dispatcher, (void*) hub) == -1)
	{
		user_manager_shutdown(hub);
		hub_free(hub);
		net_close(server_tcp);
#ifdef ADC_UDP_OPERATION
		net_close(server_udp);
#endif
		return 0;
	}
	
	hub->status = hub_status_running;
	
	return hub;
}


void hub_shutdown_service(struct hub_info* hub)
{
	hub_log(log_trace, "hub_shutdown_service()");

	event_queue_shutdown(hub->queue);
	event_del(&hub->ev_accept);
#ifdef ADC_UDP_OPERATION
	event_del(&hub->ev_datagram);
	net_close(hub->fd_udp);
#endif
	net_close(hub->fd_tcp);
	user_manager_shutdown(hub);
	hub->status = hub_status_stopped;
	hub_free(hub);
	hub = 0;
}

#define SERVER "" PRODUCT "/" VERSION ""

void hub_set_variables(struct hub_info* hub, struct acl_handle* acl)
{
	int fd, ret;
	char buf[MAX_RECV_BUF];
	char* tmp;
	
	
	hub->acl = acl;
	hub->command_info = adc_msg_construct(ADC_CMD_IINF, 15 + strlen(SERVER));
	if (hub->command_info)
	{
		adc_msg_add_named_argument(hub->command_info, ADC_INF_FLAG_CLIENT_TYPE, ADC_CLIENT_TYPE_HUB);
		adc_msg_add_named_argument(hub->command_info, ADC_INF_FLAG_USER_AGENT, SERVER);
	
		tmp = adc_msg_escape(hub->config->hub_name);
		adc_msg_add_named_argument(hub->command_info, ADC_INF_FLAG_NICK, tmp);
		hub_free(tmp);
	
		tmp = adc_msg_escape(hub->config->hub_description);
		adc_msg_add_named_argument(hub->command_info, ADC_INF_FLAG_DESCRIPTION, tmp);
		hub_free(tmp);
	}
	
	/* (Re-)read the message of the day */
	hub->command_motd = 0;
	fd = open(hub->config->file_motd, 0);
	if (fd != -1)
	{
		ret = read(fd, buf, MAX_RECV_BUF);
		if (ret > 0)
		{
			buf[ret] = 0;
			tmp = adc_msg_escape(buf);
			hub->command_motd = adc_msg_construct(ADC_CMD_IMSG, 6 + strlen(tmp));
 			adc_msg_add_argument(hub->command_motd, tmp);
			hub_free(tmp);
		}
		else
		{
			
		}
		close(fd);
	}
	
	hub->command_support = adc_msg_construct(ADC_CMD_ISUP, 6 + strlen(ADC_PROTO_SUPPORT));
	if (hub->command_support)
	{
		adc_msg_add_argument(hub->command_support, ADC_PROTO_SUPPORT);
	}
	
	hub->command_banner = adc_msg_construct(ADC_CMD_ISTA, 25 + strlen(SERVER));
	if (hub->command_banner)
	{
		adc_msg_add_argument(hub->command_banner, "000 Powered\\sby\\s" SERVER);
	}
	
	hub->status = (hub->config->hub_enabled ? hub_status_running : hub_status_disabled);
}


void hub_free_variables(struct hub_info* hub)
{
	adc_msg_free(hub->command_info);
	adc_msg_free(hub->command_banner);
	
	if (hub->command_motd)
		adc_msg_free(hub->command_motd);
	
	adc_msg_free(hub->command_support);
}


/**
 * @return 1 if nickname is in use, or 0 if not used.
 */
static inline int is_nick_in_use(struct hub_info* hub, const char* nick)
{
	struct user* lookup = get_user_by_nick(hub, nick);
	if (lookup)
	{
		return 1;
	}
	return 0;
}


/**
 * @return 1 if CID is in use, or 0 if not used.
 */
static inline int is_cid_in_use(struct hub_info* hub, const char* cid)
{
	struct user* lookup = get_user_by_cid(hub, cid);
	if (lookup)
	{
		return 1;
	}
	return 0;
}




static void set_status_code(enum msg_status_level level, int code, char buffer[4])
{
	buffer[0] = ('0' + (int) level);
	buffer[1] = ('0' + (code / 10));
	buffer[2] = ('0' + (code % 10));
	buffer[3] = 0;
}

/**
 * @param hub The hub instance this message is sent from.
 * @param user The user this message is sent to.
 * @param msg See enum status_message
 * @param level See enum status_level
 */
void hub_send_status(struct user* user, enum status_message msg, enum msg_status_level level)
{
	struct hub_config* cfg = user->hub->config;
	struct adc_message* cmd = adc_msg_construct(ADC_CMD_ISTA, 6);
	if (!cmd) return;
	char code[4];
	const char* text = 0;
	const char* flag = 0;
	char* escaped_text = 0;
	
#define STATUS(CODE, MSG, FLAG) case status_ ## MSG : set_status_code(level, CODE, code); text = cfg->MSG; flag = FLAG; break
	switch (msg)
	{
		STATUS(11, msg_hub_full, 0);
		STATUS(12, msg_hub_disabled, 0);
		STATUS(26, msg_hub_registered_users_only, 0);
		STATUS(43, msg_inf_error_nick_missing, 0);
		STATUS(43, msg_inf_error_nick_multiple, 0);
		STATUS(21, msg_inf_error_nick_invalid, 0);
		STATUS(21, msg_inf_error_nick_long, 0);
		STATUS(21, msg_inf_error_nick_short, 0);
		STATUS(21, msg_inf_error_nick_spaces, 0);
		STATUS(21, msg_inf_error_nick_bad_chars, 0);
		STATUS(21, msg_inf_error_nick_not_utf8, 0);
		STATUS(22, msg_inf_error_nick_taken, 0);
		STATUS(21, msg_inf_error_nick_restricted, 0);
		STATUS(43, msg_inf_error_cid_invalid, "FBID");
		STATUS(43, msg_inf_error_cid_missing, "FMID");
		STATUS(24, msg_inf_error_cid_taken, 0);
		STATUS(43, msg_inf_error_pid_missing, "FMPD");
		STATUS(27, msg_inf_error_pid_invalid, "FBPD");
		STATUS(31, msg_ban_permanently, 0);
		STATUS(32, msg_ban_temporarily, "TL600"); /* FIXME: Use a proper timeout */
		STATUS(23, msg_auth_invalid_password, 0);
		STATUS(20, msg_auth_user_not_found, 0);
		STATUS(30, msg_error_no_memory, 0);
		STATUS(43, msg_user_share_size_low,   "FB" ADC_INF_FLAG_SHARED_SIZE);
		STATUS(43, msg_user_share_size_high,  "FB" ADC_INF_FLAG_SHARED_SIZE);
		STATUS(43, msg_user_slots_low,        "FB" ADC_INF_FLAG_UPLOAD_SLOTS);
		STATUS(43, msg_user_slots_high,       "FB" ADC_INF_FLAG_UPLOAD_SLOTS);
		STATUS(43, msg_user_hub_limit_low, 0);
		STATUS(43, msg_user_hub_limit_high, 0);
	}
#undef STATUS
	
	escaped_text = adc_msg_escape(text);
	
	adc_msg_add_argument(cmd, code);
	adc_msg_add_argument(cmd, escaped_text);
	
	hub_free(escaped_text);
	
	if (flag)
	{
		adc_msg_add_argument(cmd, flag);
	}
	
	route_to_user(user, cmd);
	adc_msg_free(cmd);
	
}

const char* hub_get_status_message(struct hub_info* hub, enum status_message msg)
{
#define STATUS(MSG) case status_ ## MSG : return cfg->MSG; break
	struct hub_config* cfg = hub->config;
	switch (msg)
	{
		STATUS(msg_hub_full);
		STATUS(msg_hub_disabled);
		STATUS(msg_hub_registered_users_only);
		STATUS(msg_inf_error_nick_missing);
		STATUS(msg_inf_error_nick_multiple);
		STATUS(msg_inf_error_nick_invalid);
		STATUS(msg_inf_error_nick_long);
		STATUS(msg_inf_error_nick_short);
		STATUS(msg_inf_error_nick_spaces);
		STATUS(msg_inf_error_nick_bad_chars);
		STATUS(msg_inf_error_nick_not_utf8);
		STATUS(msg_inf_error_nick_taken);
		STATUS(msg_inf_error_nick_restricted);
		STATUS(msg_inf_error_cid_invalid);
		STATUS(msg_inf_error_cid_missing);
		STATUS(msg_inf_error_cid_taken);
		STATUS(msg_inf_error_pid_missing);
		STATUS(msg_inf_error_pid_invalid);
		STATUS(msg_ban_permanently);
		STATUS(msg_ban_temporarily);
		STATUS(msg_auth_invalid_password);
		STATUS(msg_auth_user_not_found);
		STATUS(msg_error_no_memory);
		STATUS(msg_user_share_size_low);
		STATUS(msg_user_share_size_high);
		STATUS(msg_user_slots_low);
		STATUS(msg_user_slots_high);
		STATUS(msg_user_hub_limit_low);
		STATUS(msg_user_hub_limit_high);
	}
#undef STATUS
	return "Unknown";
}

size_t hub_get_user_count(struct hub_info* hub)
{
	return hub->users->count;
}

size_t hub_get_max_user_count(struct hub_info* hub)
{
	return hub->config->max_users;
}

uint64_t hub_get_shared_size(struct hub_info* hub)
{
	return hub->users->shared_size;
}

uint64_t hub_get_shared_files(struct hub_info* hub)
{
	return hub->users->shared_files;
}

uint64_t hub_get_min_share(struct hub_info* hub)
{
	return 1024 * 1024 * hub->config->limit_min_share;
}

uint64_t hub_get_max_share(struct hub_info* hub)
{
	return 1024 * 1024 * hub->config->limit_max_share;
}

size_t hub_get_min_slots(struct hub_info* hub)
{
	return hub->config->limit_min_slots;
}

size_t hub_get_max_slots(struct hub_info* hub)
{
	return hub->config->limit_max_slots;
}

size_t hub_get_max_hubs_total(struct hub_info* hub)
{
	return hub->config->limit_max_hubs;
}

size_t hub_get_max_hubs_user(struct hub_info* hub)
{
	return hub->config->limit_max_hubs_user;
}

size_t hub_get_min_hubs_user(struct hub_info* hub)
{
	return hub->config->limit_min_hubs_user;
}

size_t hub_get_max_hubs_reg(struct hub_info* hub)
{
	return hub->config->limit_max_hubs_reg;
}

size_t hub_get_min_hubs_reg(struct hub_info* hub)
{
	return hub->config->limit_min_hubs_reg;
}

size_t hub_get_max_hubs_op(struct hub_info* hub)
{
	return hub->config->limit_max_hubs_op;
}

size_t hub_get_min_hubs_op(struct hub_info* hub)
{
	return hub->config->limit_min_hubs_op;
}

