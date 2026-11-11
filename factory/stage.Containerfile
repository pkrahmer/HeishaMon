# The tools this project's make check needs beyond the factory's base image: gcc and g++ for the
# host tests. Only develop counts; the build sees no files of the repository.
ARG FACTORY_BASE
FROM ${FACTORY_BASE}
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential \
 && rm -rf /var/lib/apt/lists/*
