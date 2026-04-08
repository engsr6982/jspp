import os
import shutil

BACKEND_SRC_PREFIX = "src-"

backend_name = input("Enter the name of the backend: ")
backend_prefix = input("Enter the prefix for the backend: ")

if len(backend_name) <= 0 or len(backend_prefix) <= 0:
    print("Backend name and prefix cannot be empty")
    exit(1)

backend_name_lower = backend_name.lower()
backend_name_upper = backend_name.upper()
backend_prefix_lower = backend_prefix.lower()
backend_prefix_upper = backend_prefix.upper()

if backend_name_lower.startswith(BACKEND_SRC_PREFIX):
    print("Backend name cannot start with " + BACKEND_SRC_PREFIX)
    exit(1)
if backend_prefix_lower.startswith(BACKEND_SRC_PREFIX):
    print("Backend prefix cannot start with " + BACKEND_SRC_PREFIX)
    exit(1)


backend_dir = BACKEND_SRC_PREFIX + backend_name_lower
backend_prefix_dir = BACKEND_SRC_PREFIX + backend_prefix_lower
if os.path.exists(backend_dir):
    print("Backend already exists")
    exit()
if os.path.exists(backend_prefix_dir):
    print("Backend prefix already exists")
    exit()


print(f"Creating backend: {backend_dir}")
os.mkdir(backend_dir)

STUB_BACKEND = "src-stub"

print("Copying stub backend into new backend")

shutil.copytree(STUB_BACKEND, backend_dir, dirs_exist_ok=True)

print("Backend created successfully")

print("Updating backend name and prefix...")

for root, dirs, files in os.walk(backend_dir):
    for file in files:
        # remove all .md files
        if file.endswith(".md"):
            os.remove(os.path.join(root, file))
            continue

        # rename all files that start with Stub
        new_file = file
        if file.startswith("Stub"):
            new_file = file.replace("Stub", backend_prefix)
            os.rename(os.path.join(root, file), os.path.join(root, new_file))

        # replace file contents
        with open(os.path.join(root, new_file), "r", encoding="utf-8") as f:
            content = f.read()
            if new_file.endswith(".cmake"):
                # CMake configuration needs to match the actual folder, otherwise the files cannot be found.
                content = content.replace("stub", backend_name_lower)
            else:
                content = content.replace("stub", backend_prefix_lower)
            content = content.replace("Stub", backend_prefix)
            content = content.replace("STUB", backend_name_upper)

        with open(os.path.join(root, new_file), "w", encoding="utf-8") as f:
            f.write(content)

print("Backend name and prefix updated successfully")

print("Backend creation complete")
print(f"\tfile prefix: {backend_prefix}")
print(f"\tclass prefix: {backend_prefix}")
print(f"\tnamespace prefix: {backend_prefix_lower}")
print(f"\tbackend name: {backend_name}")
print(f"\tbackend dir: {backend_dir}")
print(f"Please add backend name to CMakeLists.txt")
