FROM ubuntu:25.04

# Avoid prompts from apt
ENV DEBIAN_FRONTEND=noninteractive

# Install build essentials and gcc with C23 support
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    g++ \
    clang-format-19 \
    make \
    git \
    curl \
    zsh \
    && rm -rf /var/lib/apt/lists/*

# Install starship prompt
RUN curl -fsSL https://starship.rs/install.sh | sh -s -- -y \
    && echo '\neval "$(starship init zsh)"' >> /etc/zsh/zshrc \
    && echo '\neval "$(starship init bash)"' >> /etc/bash.bashrc

# Copy custom starship config
COPY starship.toml /root/.config/starship.toml

# Set the working directory
WORKDIR /app

# Set zsh as the default shell
SHELL ["/usr/bin/zsh", "-c"]

# Set default command to zsh
CMD [ "zsh" ]