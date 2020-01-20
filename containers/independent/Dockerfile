FROM 'ubuntu:latest'

RUN rm -f /tmp/*.tar.gz
COPY *.tar.gz /tmp
RUN mkdir -p /software && tar -C /software -xzvf /tmp/*.tar.gz && rm -f /tmp/*.tar.gz
RUN cat /software/absl/version.txt
CMD cat /software/absl/version.txt
