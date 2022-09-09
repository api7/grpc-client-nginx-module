FROM golang:1.19-bullseye

ADD . /usr/local/backend
WORKDIR /usr/local/backend
ARG ENABLE_PROXY
RUN if [ "$ENABLE_PROXY" = "true" ] ; then go env -w GOPROXY=https://goproxy.io,direct ; fi \
    && go env \
    && go build
ENTRYPOINT [ "/usr/local/backend/backend" ]
