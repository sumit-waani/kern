/*
 * cmd_new.c - 'kern new <name>' command
 *
 * Scaffolds a new kern project with the standard directory layout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

/* Create a directory, return 0 on success or if it already exists */
static int make_dir(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Error: could not create directory '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    return 0;
}

/* Create nested directories */
static int make_dirs(const char *path) {
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (make_dir(tmp) != 0) return -1;
            *p = '/';
        }
    }
    return make_dir(tmp);
}

/* Write content to a file */
static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "Error: could not create file '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
    fputs(content, f);
    fclose(f);
    return 0;
}

/* Print a creation message with checkmark */
static void print_created(const char *project_name, const char *relative_path) {
    printf("  \xe2\x9c\x93 %s/%s\n", project_name, relative_path);
}

int cmd_new(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: kern new <name>\n");
        fprintf(stderr, "  Create a new kern project in the specified directory.\n");
        return 1;
    }

    const char *name = argv[1];

    /* Validate project name */
    if (strlen(name) == 0) {
        fprintf(stderr, "Error: project name cannot be empty\n");
        return 1;
    }

    /* Check if directory already exists and is non-empty */
    struct stat st;
    if (stat(name, &st) == 0) {
        fprintf(stderr, "Error: '%s' already exists\n", name);
        return 1;
    }

    printf("\n  Creating new kern project: %s\n\n", name);

    /* Extract just the base name for use in file content */
    const char *base_name = strrchr(name, '/');
    base_name = base_name ? base_name + 1 : name;

    /* Create directory structure */
    char path[2048];

    if (make_dirs(name) != 0) return 1;

    snprintf(path, sizeof(path), "%s/pages", name);
    if (make_dirs(path) != 0) return 1;

    snprintf(path, sizeof(path), "%s/views/layouts", name);
    if (make_dirs(path) != 0) return 1;

    snprintf(path, sizeof(path), "%s/views/pages", name);
    if (make_dirs(path) != 0) return 1;

    snprintf(path, sizeof(path), "%s/assets/css", name);
    if (make_dirs(path) != 0) return 1;

    snprintf(path, sizeof(path), "%s/db/migrations", name);
    if (make_dirs(path) != 0) return 1;

    /* kern.toml */
    snprintf(path, sizeof(path), "%s/kern.toml", name);
    char content[4096];
    snprintf(content, sizeof(content),
        "[app]\n"
        "name = \"%s\"\n"
        "port = 3000\n"
        "\n"
        "[db]\n"
        "path = \"db/%s.sqlite\"\n",
        base_name, base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "kern.toml");

    /* app.c */
    snprintf(path, sizeof(path), "%s/app.c", name);
    snprintf(content, sizeof(content),
        "#include <kern.h>\n"
        "\n"
        "int main(int argc, char **argv) {\n"
        "    (void)argc; (void)argv;\n"
        "    kern_app_t *app = kern_app_new(\"%s\");\n"
        "    kern_app_listen(app, 3000);\n"
        "    return kern_app_run(app);\n"
        "}\n",
        base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "app.c");

    /* pages/index.c */
    snprintf(path, sizeof(path), "%s/pages/index.c", name);
    snprintf(content, sizeof(content),
        "#include <kern.h>\n"
        "\n"
        "/* GET / */\n"
        "kern_response_t *page_index(kern_req_t *req) {\n"
        "    (void)req;\n"
        "    kern_response_t *res = kern_response_new(200);\n"
        "    kern_response_header(res, \"Content-Type\", \"text/html\");\n"
        "    kern_response_body_str(res, \"<h1>Welcome to %s!</h1>\");\n"
        "    return res;\n"
        "}\n",
        base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "pages/index.c");

    /* views/layouts/base.khtml */
    snprintf(path, sizeof(path), "%s/views/layouts/base.khtml", name);
    snprintf(content, sizeof(content),
        "doctype html\n"
        "html(lang=\"en\")\n"
        "  head\n"
        "    meta(charset=\"utf-8\")\n"
        "    title #{title}\n"
        "    link(rel=\"stylesheet\" href=\"/assets/css/app.css\")\n"
        "  body\n"
        "    block content\n");
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "views/layouts/base.khtml");

    /* views/pages/home.khtml */
    snprintf(path, sizeof(path), "%s/views/pages/home.khtml", name);
    snprintf(content, sizeof(content),
        "extend layouts/base\n"
        "\n"
        "block content\n"
        "  h1 Welcome to %s\n"
        "  p Your kern app is running.\n",
        base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "views/pages/home.khtml");

    /* assets/css/app.css */
    snprintf(path, sizeof(path), "%s/assets/css/app.css", name);
    snprintf(content, sizeof(content),
        "/* %s - main stylesheet */\n"
        "body {\n"
        "    font-family: system-ui, sans-serif;\n"
        "    max-width: 800px;\n"
        "    margin: 0 auto;\n"
        "    padding: 2rem;\n"
        "}\n",
        base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "assets/css/app.css");

    /* db/migrations/.gitkeep */
    snprintf(path, sizeof(path), "%s/db/migrations/.gitkeep", name);
    if (write_file(path, "") != 0) return 1;
    print_created(base_name, "db/migrations/.gitkeep");

    /* .gitignore */
    snprintf(path, sizeof(path), "%s/.gitignore", name);
    snprintf(content, sizeof(content),
        "build/\n"
        "dist/\n"
        "public/assets/\n"
        "*.o\n"
        "*.sqlite\n");
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, ".gitignore");

    /* README.md */
    snprintf(path, sizeof(path), "%s/README.md", name);
    snprintf(content, sizeof(content),
        "# %s\n"
        "\n"
        "A web application built with [kern](https://github.com/sumit-waani/kern).\n"
        "\n"
        "## Getting Started\n"
        "\n"
        "```bash\n"
        "kern dev\n"
        "```\n"
        "\n"
        "Open http://localhost:3000 in your browser.\n"
        "\n"
        "## Build for Production\n"
        "\n"
        "```bash\n"
        "kern build\n"
        "```\n"
        "\n"
        "The compiled binary will be in `dist/%s`.\n",
        base_name, base_name);
    if (write_file(path, content) != 0) return 1;
    print_created(base_name, "README.md");

    printf("\n  Done! Run:\n");
    printf("    cd %s && kern dev\n\n", name);

    return 0;
}
