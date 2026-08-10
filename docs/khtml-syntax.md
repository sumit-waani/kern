# KHTML Template Syntax

The `.khtml` template engine is a Pug-derived, indentation-based syntax that compiles to C at build time. There is zero runtime parsing; templates are compiled to C functions that emit HTML directly.

## File Extensions

| Extension | Purpose |
|-----------|---------|
| `.khtml` | HTML templates (full pages or components) |
| `.ktxt` | Plain text templates (emails, notifications) |
| `.kfrag` | Partials/fragments (included by other templates) |

## Basic Syntax

### Tags

Tags are written without angle brackets. The tag name starts the line:

```pug
h1 Hello World
p This is a paragraph.
div
  span Nested content
```

**Output:**
```html
<h1>Hello World</h1>
<p>This is a paragraph.</p>
<div>
  <span>Nested content</span>
</div>
```

### Attributes

Attributes are enclosed in parentheses after the tag name:

```pug
a(href="/posts" class="text-blue-500") View Posts
input(type="email" name="email" required placeholder="you@example.com")
img(src="/logo.png" alt="Logo" width="200")
```

**Output:**
```html
<a href="/posts" class="text-blue-500">View Posts</a>
<input type="email" name="email" required placeholder="you@example.com">
<img src="/logo.png" alt="Logo" width="200">
```

### Dynamic Attributes

Use `{expr}` for dynamic attribute values:

```pug
a(href={url("posts.show", post->id)}) #{post->title}
link(rel="stylesheet" href={asset("css/app.css")})
img(src={user->avatar_url} alt={user->name})
```

### Nesting (Indentation)

Indentation determines the parent-child relationship. Use consistent spaces (2 or 4):

```pug
div(class="container")
  header
    h1 My App
    nav
      a(href="/") Home
      a(href="/about") About
  main
    p Content goes here
  footer
    p Copyright 2025
```

### Self-Closing Tags

Void elements (img, input, br, hr, meta, link) are automatically self-closed:

```pug
br
hr
input(type="text" name="q")
meta(charset="utf-8")
```

**Output:**
```html
<br>
<hr>
<input type="text" name="q">
<meta charset="utf-8">
```

### Doctype

```pug
doctype html
```

**Output:**
```html
<!doctype html>
```

---

## Interpolation

### Escaped Interpolation `#{expr}`

Interpolates a C expression with automatic HTML escaping (prevents XSS):

```pug
h1 Hello, #{user->name}!
p You have #{message_count} messages.
span Posted #{format_date(post->created_at)}
```

Characters `<`, `>`, `&`, `"`, and `'` are escaped automatically.

### Unescaped (Raw) Interpolation `!{expr}`

For trusted HTML content that should NOT be escaped:

```pug
div(class="prose")
  !{post->rendered_html}
```

**Warning:** Only use `!{expr}` for content you trust completely. User-supplied content must always use `#{expr}`.

### Interpolation in Attributes

Use `{expr}` (without the `#` prefix) for dynamic attribute values:

```pug
a(href={kern_url("posts.show", post->id)}) View
div(class={active ? "bg-blue-500" : "bg-gray-200"})
```

---

## Control Flow

All control flow uses the `-` prefix to indicate a C statement.

### Conditionals

```pug
- if (user != NULL)
  p Welcome back, #{user->name}!
  a(href="/logout") Logout
- else
  p Please sign in.
  a(href="/login") Login

- if (posts_len == 0)
  p(class="text-zinc-500") No posts yet.
- else if (posts_len == 1)
  p You have one post.
- else
  p You have #{posts_len} posts.
```

### For Loops

```pug
ul(class="space-y-2")
  - for (size_t i = 0; i < posts_len; i++)
    li
      a(href={url("posts.show", posts[i]->id)})
        | #{posts[i]->title}
      span(class="text-zinc-400 text-sm") #{format_date(posts[i]->created_at)}
```

### While Loops

```pug
- size_t i = 0;
- while (i < 5)
  p Item #{i}
  - i++;
```

### Switch/Case (via if-else)

kern templates do not have a dedicated switch syntax. Use chained if-else:

```pug
- if (strcmp(status, "active") == 0)
  span(class="badge badge-green") Active
- else if (strcmp(status, "pending") == 0)
  span(class="badge badge-yellow") Pending
- else
  span(class="badge badge-red") Inactive
```

---

## Text Content

### Inline Text

Text after a tag name (with a space) becomes the tag's content:

```pug
p This is paragraph text.
h1 Page Title
```

### Block Text with `|`

The pipe character `|` starts a literal text line:

```pug
p
  | This is a longer paragraph
  | that spans multiple lines
  | in the template source.
```

**Output:**
```html
<p>This is a longer paragraph that spans multiple lines in the template source.</p>
```

### Mixed Content

Combine tags and text freely:

```pug
p
  | Written by
  strong #{author->name}
  | on #{format_date(post->created_at)}.
```

**Output:**
```html
<p>Written by <strong>Alice</strong> on Jan 15, 2025.</p>
```

---

## Includes

### Basic Include

Pull in another template file (`.kfrag` for partials):

```pug
doctype html
html(lang="en")
  head
    title #{page_title}
  body
    include partials/nav.kfrag
    main
      block body
    include partials/footer.kfrag
```

### Include Path Resolution

Paths are relative to the `views/` directory:

- `include partials/nav.kfrag` resolves to `views/partials/nav.kfrag`
- `include components/ui/button.khtml` resolves to `views/components/ui/button.khtml`

A missing include is a **build error** (caught at compile time, not runtime).

---

## Layout Inheritance

### Defining a Layout

Create a base layout with named `block` regions:

```pug
// views/layouts/base.khtml
doctype html
html(lang="en")
  head
    block head
      meta(charset="utf-8")
      meta(name="viewport" content="width=device-width, initial-scale=1")
      title #{page_title} - MyApp
      link(rel="stylesheet" href={asset("css/app.css")})
  body(class="bg-zinc-50 text-zinc-900")
    include partials/nav.kfrag
    main(class="container mx-auto p-8")
      block body
    include partials/footer.kfrag
    block scripts
```

### Extending a Layout

Child templates use `extend` and fill `block` regions:

```pug
// views/pages/home.khtml
extend layouts/base

block head
  title Home - MyApp
  meta(name="description" content="Welcome to MyApp")

block body
  h1(class="text-3xl font-bold") Welcome!
  p This is the home page.

block scripts
  script(src={asset("js/home.js")} defer)
```

### Block Rules

- A `block` in the parent provides **default content** (used if the child does not override it)
- A `block` in the child **replaces** the parent's content entirely
- Blocks cannot be nested within control flow (they must be at the top level of the extending template)
- You can only `extend` one layout per template
- `extend` must be the first non-comment line in the template

---

## Comments

### HTML Comments (output in HTML)

```pug
// This comment appears in the HTML output
div Content
```

**Output:**
```html
<!-- This comment appears in the HTML output -->
<div>Content</div>
```

### Silent Comments (not in output)

```pug
//- This comment is NOT in the output
div Content
```

---

## Complete Example

Here is a full page template demonstrating all major features:

```pug
// views/pages/posts/index.khtml
extend layouts/base

block head
  title Posts - MyApp

block body
  div(class="flex justify-between items-center mb-8")
    h1(class="text-3xl font-bold") Posts

    - if (current_user != NULL)
      a(href="/posts/new" class="bg-zinc-900 text-white px-4 py-2 rounded") New Post

  - if (posts_len == 0)
    div(class="text-center py-12")
      p(class="text-zinc-500 text-lg") No posts yet.
      - if (current_user != NULL)
        a(href="/posts/new" class="text-blue-600 underline") Create your first post

  - else
    div(class="space-y-6")
      - for (size_t i = 0; i < posts_len; i++)
        article(class="border rounded-lg p-6 hover:shadow-md transition")
          h2(class="text-xl font-semibold")
            a(href={url("posts.show", posts[i]->id)} class="hover:text-blue-600")
              | #{posts[i]->title}
          p(class="text-zinc-600 mt-2") #{posts[i]->excerpt}
          div(class="flex items-center gap-4 mt-4 text-sm text-zinc-400")
            span By #{posts[i]->author_name}
            span #{format_date(posts[i]->created_at)}

    //- Pagination
    - if (total_pages > 1)
      nav(class="flex justify-center gap-2 mt-8")
        - for (size_t p = 1; p <= total_pages; p++)
          a(href={urlf("/posts?page=%zu", p)}
            class={p == current_page ? "bg-zinc-900 text-white px-3 py-1 rounded" : "px-3 py-1 rounded hover:bg-zinc-100"})
            | #{p}
```

## Compile Output

The above template compiles to a C function like:

```c
/* AUTO-GENERATED. DO NOT EDIT. */
#include <kern.h>

kern_response_t *kern_render_pages_posts_index(kern_req_t *req, kern_dict_t *vars) {
    kern_buf_t *buf = kern_buf_new(4096);
    // ... generated HTML building code ...
    kern_response_t *res = kern_response_from_buf(buf, "text/html; charset=utf-8");
    return res;
}
```

Key properties of the generated code:
- No runtime parsing or interpretation
- All `#{expr}` calls go through `kern_html_write_esc()` (XSS-safe)
- Type errors in expressions are caught at C compile time
- Missing includes are build errors
- Zero allocations in the hot path (uses pre-allocated buffer)

## Template Variables

Variables are passed from the handler via `kern_dict_t`:

```c
// In the page handler
return kern_render(req, "posts/index", KERN_T(
    "posts",        post_list,
    "posts_len",    (size_t)count,
    "current_page", (size_t)page,
    "total_pages",  (size_t)total,
    "current_user", kern_current_user(req)
));
```

In the template, these variables are available directly by name (the compiler generates the appropriate `kern_dict_get()` calls).

## Built-in Template Helpers

| Helper | Purpose | Example |
|--------|---------|---------|
| `asset(path)` | Get fingerprinted asset URL | `{asset("css/app.css")}` |
| `url(route, params...)` | Generate URL for a named route | `{url("posts.show", id)}` |
| `urlf(fmt, ...)` | Printf-style URL generation | `{urlf("/posts?page=%d", p)}` |
| `csrf_token()` | Get the CSRF token string | `{csrf_token()}` |
| `flash()` | Get flash message (or NULL) | `#{flash()}` |
| `format_date(ts)` | Format a unix timestamp | `#{format_date(created_at)}` |
