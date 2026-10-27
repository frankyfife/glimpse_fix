// setlaa.exe <SplitSecond.exe> - sets the LARGE_ADDRESS_AWARE flag (4 GB address space for the
// 32-bit game) and keeps the untouched file as <name>.orig. Drag the exe onto it or pass the path.
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void PauseIfOwnConsole(void)
{
    DWORD procs[2];
    if (GetConsoleProcessList(procs, 2) == 1) {  // started by double click or drag and drop
        printf("\nPress Enter to close.");
        getchar();
    }
}

int main(int argc, char** argv)
{
    char path[MAX_PATH] = "SplitSecond.exe", backup[MAX_PATH + 8];
    int rc = 1;
    if (argc > 1) strncpy_s(path, sizeof(path), argv[1], _TRUNCATE);
    FILE* f = NULL;
    if (fopen_s(&f, path, "r+b") || !f) {
        printf("cannot open %s\n", path);
        goto out;
    }
    IMAGE_DOS_HEADER dos;
    IMAGE_FILE_HEADER fh;
    DWORD sig;
    if (fread(&dos, sizeof(dos), 1, f) != 1 || dos.e_magic != IMAGE_DOS_SIGNATURE || fseek(f, dos.e_lfanew, SEEK_SET) ||
        fread(&sig, 4, 1, f) != 1 || sig != IMAGE_NT_SIGNATURE || fread(&fh, sizeof(fh), 1, f) != 1 ||
        fh.Machine != IMAGE_FILE_MACHINE_I386) {
        printf("%s is not a 32-bit Windows executable\n", path);
        goto out;
    }
    if (fh.Characteristics & IMAGE_FILE_LARGE_ADDRESS_AWARE) {
        printf("%s already has the LAA flag, nothing to do\n", path);
        rc = 0;
        goto out;
    }
    fclose(f);
    f = NULL;
    sprintf_s(backup, sizeof(backup), "%s.orig", path);
    if (!CopyFileA(path, backup, TRUE) && GetLastError() != ERROR_FILE_EXISTS) {
        printf("cannot write backup %s\n", backup);
        goto out;
    }
    if (fopen_s(&f, path, "r+b") || !f) goto out;
    fh.Characteristics |= IMAGE_FILE_LARGE_ADDRESS_AWARE;
    fseek(f, dos.e_lfanew + 4, SEEK_SET);
    if (fwrite(&fh, sizeof(fh), 1, f) == 1) {
        printf("LAA flag set on %s (backup: %s)\n", path, backup);
        rc = 0;
    }
out:
    if (f) fclose(f);
    PauseIfOwnConsole();
    return rc;
}
