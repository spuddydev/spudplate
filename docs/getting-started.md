# Getting Started

A five-minute tour from install to your first running template.

---

## 1. Install spudplate

One-liner for Linux x86_64 and macOS arm64:

```
curl -fsSL https://raw.githubusercontent.com/spuddydev/spudplate/main/install.sh | sh
```

The binary lands in `~/.local/bin`. Set `PREFIX=/usr/local` (and run with `sudo`) to install system-wide, or `VERSION=v0.1.0` to pin to a specific release.

Other options:

- Download a binary from the [releases page](https://github.com/spuddydev/spudplate/releases) and put it on your `PATH`. Each release ships an `SHA256SUMS` you can verify against.
- Build from source. You need CMake and a C++20 compiler.

  ```
  cmake -B build && cmake --build build
  sudo cmake --install build
  ```

Confirm the install:

```
spudplate version
```

---

## 2. Write a tiny template

Create `hello.spud`:

```
ask name "Your name?" string

mkdir hello as dir
file dir/greeting.txt content "Hello, " + name + "!\n"
```

Three things are happening here:

1. `ask` prompts the user and binds the answer to `name`.
2. `mkdir hello as dir` creates a directory and binds its path to `dir` so later statements can refer back to it.
3. `file dir/greeting.txt content ...` writes a file using a string expression.

---

## 3. Install the template

```
spudplate install hello.spud
```

Spudplate parses, validates, and bundles every asset the template references into a single `.spp` file under your install root. The default location is `~/.local/share/spudplate/hello.spp`.

You can also validate without installing:

```
spudplate validate hello.spud
```

---

## 4. Run it

```
spudplate run hello
```

You'll see:

```
[1/1] Your name?
> Joe
```

After answering, spudplate creates `hello/greeting.txt` in the current working directory.

Run it again from anywhere - the bundled assets travel with the template, so the run is cwd-independent.

---

## 5. Where to next

- **The full language:** [Spudlang Syntax Reference](syntax.md). Path expressions, conditional actions, repeats, includes, and the rest.
- **Every CLI flag:** [CLI Reference](cli.md).
- **The wire format:** [Spudpack Binary Format](spudpack-format.md), if you're curious how `.spp` files are laid out.
- **Sharing templates:** send the `.spp` file. The recipient runs it directly with `spudplate run path/to/template.spp` - no install step required.

If something is missing or broken, the [issue tracker](https://github.com/spuddydev/spudplate/issues) is the right place to flag it.
