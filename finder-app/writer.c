#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
  openlog("finder-app-writer", LOG_PID, LOG_USER);

  if (argc < 3) {
    syslog(
        LOG_ERR,
        "Error : missing arguments. Usage : %s <file_path> <string_to_write>",
        argv[0]);
    fprintf(stderr, "missing args. check sys logs for details.\n");
    closelog();
    return EXIT_FAILURE;
  }

  char *writefile = argv[1];
  char *writestr = argv[2];

  syslog(LOG_DEBUG, "attempting to open %s", writefile);
  FILE *file = fopen(writefile, "w");
  if (file == NULL) {
    syslog(LOG_ERR, "Error opening the file %s : %s", writefile, strerror(errno));
    fprintf(stderr,
            "Error: Failed to open file. Check system logs for details.\n");
    closelog();
    return EXIT_FAILURE;
  }

  syslog(LOG_DEBUG, "Attempting to write the string %s to file : %s", writestr,
         writefile);
  if (fputs(writestr, file) == EOF) {
    syslog(LOG_ERR, "error writing data to file %s : %s", writefile,
           strerror(errno));
    fprintf(stderr,
            "Error: Failed to write to file. Check system logs for details.\n");
    fclose(file);
    closelog();
    return EXIT_FAILURE;
  }
  fclose(file);
  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
  closelog();
  return EXIT_SUCCESS;
}
