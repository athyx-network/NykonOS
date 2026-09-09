import os
import glob
import re

APPS_DIR = "apps" if os.path.basename(os.getcwd()) == "src" else "src/apps"
OUT_FILE = "sys/kernel/app_registry.c" if os.path.basename(os.getcwd()) == "src" else "src/sys/kernel/app_registry.c"

def main():
    if not os.path.exists(APPS_DIR):
        print(f"Directory {APPS_DIR} does not exist.")
        return

    c_files = glob.glob(os.path.join(APPS_DIR, "**/*.c"), recursive=True)
    app_structs = []

    # Regex to find NykonApp definitions
    # Matches: NykonApp app_demo = {
    regex = re.compile(r"NykonApp\s+([a-zA-Z0-9_]+)\s*=")

    for fpath in c_files:
        with open(fpath, "r") as f:
            content = f.read()
            matches = regex.findall(content)
            for m in matches:
                app_structs.append(m)

    with open(OUT_FILE, "w") as f:
        f.write('#include "../../sys/nykon_api.h"\n\n')
        
        for struct_name in app_structs:
            f.write(f"extern NykonApp {struct_name};\n")
            
        f.write("\nNykonApp* registered_apps[32] = {\n")
        
        for i, struct_name in enumerate(app_structs):
            f.write(f"    &{struct_name}")
            if i < len(app_structs) - 1:
                f.write(",")
            f.write("\n")
            
        f.write("};\n\n")
        f.write(f"int num_registered_apps = {len(app_structs)};\n")

if __name__ == "__main__":
    main()
