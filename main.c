#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(const char *program) {
    fprintf(stderr,
        "Usage:\n"
        "  %s web [region_server.py options...]\n"
        "  %s pipeline <runner> [runner arguments...]\n"
        "  %s replay <replay_runner> [replay arguments...]\n"
        "\n"
        "Examples:\n"
        "  %s web --host 0.0.0.0 --port 9191 --regions configs/regions.json\n"
        "  %s pipeline build-rknn/pipeline/posevent_rknn_live <rtsp> <model> /dev/fb0 configs/regions.json\n",
        program, program, program, program, program);
}

static int run_python(int argc, char **argv) {
    char **command = calloc((size_t)argc + 3U, sizeof(*command));
    if (!command) { perror("calloc"); return  1; }
    command[0] = "python3";
    command[1] = getenv("POSEVENT_REGION_SERVER");
    if (!command[1] || command[1][0] == '\0') command[1] = "region_server.py";
    for (int i = 0; i < argc; ++i) command[i + 2] = argv[i];
    command[argc + 2] = NULL;
    execvp(command[0], command);
    perror("exec python region server");
    free(command);
    return 127;
}

static int run_binary(int argc, char **argv) {
    if (argc < 1 || !argv[0] || argv[0][0] == '\0') {
        fprintf(stderr, "runner path is required\n");
        return 2;
    }
    execvp(argv[0], argv);
    fprintf(stderr, "exec %s: %s\n", argv[0], strerror(errno));
    return 127;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]); return 0;
    }
    if (strcmp(argv[1], "web") == 0) return run_python(argc - 2, argv + 2);
    if (strcmp(argv[1], "pipeline") == 0) return run_binary(argc - 2, argv + 2);
    if (strcmp(argv[1], "replay") == 0) return run_binary(argc - 2, argv + 2);

    fprintf(stderr, "unknown command: %s\n", argv[1]);
    usage(argv[0]);
    return 2;
}
