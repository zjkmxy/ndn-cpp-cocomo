# [Choice] Python version: 3, 3.8, 3.7, 3.6
ARG VARIANT=3
FROM ghcr.io/zjkmxy/nfd-codespace:22.02

# [Optional] If your pip requirements rarely change, uncomment this section to add them to the image.
# COPY requirements.txt /tmp/pip-tmp/
# RUN pip3 --disable-pip-version-check --no-cache-dir install -r /tmp/pip-tmp/requirements.txt \
#    && rm -rf /tmp/pip-tmp

# [Optional] Uncomment this section to install additional OS packages.
RUN echo "deb http://http.us.debian.org/debian/ testing non-free contrib main" >> /etc/apt/sources.list \
    && apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends g++-11

# [Optional] Uncomment this line to install global node packages.
# RUN su vscode -c "source /usr/local/share/nvm/nvm.sh && npm install -g <your-package-here>" 2>&1

RUN pip3 --disable-pip-version-check --no-cache-dir install python-ndn && rm -rf /tmp/pip-tmp

RUN ndnsec-keygen /test | ndnsec-install-cert -