import os
import sys
import subprocess
import glob
import shutil

if len(sys.argv) != 3:
    print("Usage: scripts/create_local_extension_repo.py <path/to/duckdb/build> ")
    exit(1)

src_path = sys.argv[1]

# Generate queries for platform and version
duckdb_path = os.path.join(src_path, 'duckdb')
duckdb_invocation = [duckdb_path, '-noheader', '-list', '-c']
platform_query = duckdb_invocation.copy()
platform_query.append('SELECT platform FROM pragma_platform();')
version_query = duckdb_invocation.copy()
version_query.append('SELECT library_version FROM pragma_version();')
source_id_query = duckdb_invocation.copy()
source_id_query.append('SELECT source_id FROM pragma_version();')

# Run queries to fetch duckdb platform and version
res = subprocess.run(platform_query, check=True, capture_output=True)
duckdb_platform = res.stdout.decode('ascii').strip()
res = subprocess.run(version_query, check=True, capture_output=True)
duckdb_version = res.stdout.decode('ascii').strip()
res = subprocess.run(source_id_query, check=True, capture_output=True)
source_id = res.stdout.decode('ascii').strip()

version_path = source_id if "-dev" in duckdb_version else duckdb_version

# Create destination path
dest_path = os.path.join(sys.argv[2], version_path, duckdb_platform)
if not os.path.exists(dest_path):
    os.makedirs(dest_path)

# Now copy over the extensions to the correct path
glob_string = os.path.join(src_path, 'extension', '*', '*.duckdb_extension')
for file in glob.glob(glob_string):
    dest_file = os.path.join(dest_path, os.path.basename(file))
    shutil.copy(file, dest_file)
