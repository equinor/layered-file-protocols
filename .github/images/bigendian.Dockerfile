FROM s390x/debian as setup
RUN useradd --create-home --shell /bin/bash ci
RUN apt-get update
RUN apt-get install -y cmake g++ git libfmt-dev

FROM setup
WORKDIR /home/ci
COPY --chown=ci:ci . /home/ci/lfp
WORKDIR /home/ci/lfp/build
RUN cmake -DBUILD_SHARED_LIBS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release ..
RUN make -j4
RUN ctest --output-on-failure
