/* L4: CLIP static command registration + ExoKit newline command convention.
 * This S1 grammar intentionally accepts one unquoted token and no arguments.
 */
#include "console_commands.h"
#include "board_console.h"
#include "s1_config.h"
#include "clip.h"
#include <stdio.h>
#include <string.h>

static char line[S1_LINE_MAX + 1U];
static size_t used;
static bool saw_cr, invalid_line, long_line;

typedef struct {
    uint32_t now_ms;
    uint32_t overruns;
} CommandContext;

static void reply(const char *text)
{
    /* Poll reserved a whole response slot. Only this main-loop producer writes.
     * A false result can still occur if an IRQ has latched a transport fault.
     */
    (void)BoardConsole_Write((const uint8_t *)text, strlen(text));
}

static void command_reply(const struct clip *self, const struct clip_command *cmd,
                          size_t argc, struct clip_arg_value argv[], void *context)
{
    (void)self;
    (void)argv;
    if (argc != 0U) { reply("ERR args\r\n"); return; }
    if (strcmp(cmd->name, "ping") == 0) {
        reply("OK pong\r\n");
    } else if (strcmp(cmd->name, "status") == 0) {
        const CommandContext *ctx = context;
        BoardConsoleStats stats;
        char out[S1_REPLY_SIZE];
        BoardConsole_GetStats(&stats);
        int n = snprintf(out, sizeof(out),
            "OK stage=S1_COMM uptime_ms=%lu output_allowed=0 uart_errors=%lu "
            "rx_overflows=%lu tx_timeouts=%lu scheduler_overruns=%lu\r\n",
            (unsigned long)ctx->now_ms, (unsigned long)stats.uart_errors,
            (unsigned long)stats.rx_overflows, (unsigned long)stats.tx_timeouts,
            (unsigned long)ctx->overruns);
        if (n > 0 && (size_t)n < sizeof(out)) { reply(out); }
        else { reply("ERR format\r\n"); }
    } else {
        reply("OK commands=ping,status,help,?\r\n");
    }
}

static void command_event(const struct clip *self, clip_event_t event,
                          union clip_event_arg *arg, void *context)
{
    (void)self; (void)arg; (void)context;
    if (event == CLIP_EVENT_HELP) {
        reply("OK commands=ping,status,help,?\r\n");
    } else if (event == CLIP_EVENT_COMMAND_NOT_FOUND) {
        reply("ERR unknown\r\n");
    } else if (event == CLIP_EVENT_ARGUMENTS_ERROR) {
        reply("ERR args\r\n");
    }
}

static CLIP_DEF_ROOT_COMMAND(ping_cmd, "ping", "link check", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(status_cmd, "status", "S1 status", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT_COMMAND(help_cmd, "help", "command list", command_reply)
CLIP_DEF_ROOT_COMMAND_END()
static CLIP_DEF_ROOT(commands, NULL, command_event)
    CLIP_DEF_ADD_ROOT_COMMAND(&ping_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&status_cmd)
    CLIP_DEF_ADD_ROOT_COMMAND(&help_cmd)
CLIP_DEF_ROOT_END()

static bool dispatch_line(CommandContext *context)
{
    if (long_line) { reply("ERR line_too_long\r\n"); return true; }
    if (invalid_line) { reply("ERR syntax\r\n"); return true; }
    line[used] = '\0';
    char *token = line;
    while (*token == ' ') { token++; }
    if (*token == '\0') { return false; }
    char *end = token;
    while (*end != '\0' && *end != ' ') {
        if (!((*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z')
              || *end == '?')) { reply("ERR syntax\r\n"); return true; }
        end++;
    }
    char *rest = end;
    while (*rest == ' ') { rest++; }
    /* Reject '? extra' and 'ping ""' before CLIP's help/argument shortcuts. */
    if (*rest != '\0') { reply("ERR args\r\n"); return true; }
    *end = '\0';
    if (!BoardConsole_HasFault()) {
        clip_cmd_parse_line(&commands, NULL, token, context);
    }
    return true;
}

void ConsoleCommands_Init(void)
{
    used = 0U;
    saw_cr = false;
    invalid_line = false;
    long_line = false;
}

void ConsoleCommands_Poll(uint32_t now_ms, uint32_t scheduler_overruns)
{
    CommandContext context = {now_ms, scheduler_overruns};
    if (BoardConsole_TxFree() < S1_REPLY_SIZE || BoardConsole_HasFault()) { return; }
    for (size_t i = 0U; i < S1_RX_BUDGET; i++) {
        uint8_t ch;
        if (BoardConsole_Read(&ch, 1U) != 1U) { break; }
        if (ch == '\n') {
            bool handled = dispatch_line(&context);
            ConsoleCommands_Init();
            if (handled) { break; }
        } else if (!invalid_line && !long_line) {
            if (saw_cr) { invalid_line = true; }
            else if (ch == '\r') { saw_cr = true; }
            else if (ch < 0x20U || ch > 0x7EU) { invalid_line = true; }
            else if (used == S1_LINE_MAX) { long_line = true; }
            else { line[used++] = (char)ch; }
        }
    }
}
