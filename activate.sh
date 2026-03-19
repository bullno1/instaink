#!/usr/bin/env sh
# Usage: source activate.sh
# Sets up ESP-IDF environment for this project session.

_find_esp_idf() {
    # 1. Already activated
    if [ -n "$IDF_PATH" ] && [ -f "$IDF_PATH/export.sh" ]; then
        echo "$IDF_PATH"
        return 0
    fi

    # 2. Common install paths (order: distro pkg → user install → CI)
    for _candidate in \
        /opt/esp-idf \
        "$HOME/esp/esp-idf" \
        "$HOME/.espressif/esp-idf" \
        /usr/local/esp-idf \
        /opt/homebrew/opt/esp-idf
    do
        if [ -f "$_candidate/export.sh" ]; then
            echo "$_candidate"
            return 0
        fi
    done

    # 3. Locate idf.py in PATH and walk up to find export.sh
    _idf_py=$(command -v idf.py 2>/dev/null)
    if [ -n "$_idf_py" ]; then
        _dir=$(dirname "$(realpath "$_idf_py")")
        while [ "$_dir" != "/" ]; do
            if [ -f "$_dir/export.sh" ]; then
                echo "$_dir"
                return 0
            fi
            _dir=$(dirname "$_dir")
        done
    fi

    return 1
}

# Main
_idf_path=$(_find_esp_idf)
if [ -z "$_idf_path" ]; then
    echo "ERROR: ESP-IDF not found. Install it or set IDF_PATH manually:"
    echo "  export IDF_PATH=/your/path/to/esp-idf"
    return 1
fi

if [ -n "$IDF_PATH" ]; then
    echo "ESP-IDF already active: $IDF_PATH"
    return 0
fi

echo "Activating ESP-IDF from: $_idf_path"
# shellcheck disable=SC1091
. "$_idf_path/export.sh"

unset _idf_path _sourced _find_esp_idf

IDF_CCACHE_ENABLE=1
