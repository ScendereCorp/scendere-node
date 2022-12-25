FROM scenderecurrency/scendere-env:gcc

ARG NETWORK=live
ARG TRAVIS_TAG=DEV_BUILD
ARG CI_BUILD=OFF
ADD ./ /tmp/src

RUN mkdir /tmp/build && \
cd /tmp/build && \
cmake /tmp/src -DCI_BUILD=${CI_BUILD} \
-DBOOST_ROOT=${BOOST_ROOT} -DPORTABLE=1 \
-DACTIVE_NETWORK=scendere_${NETWORK}_network \
-DSCENDERE_POW_SERVER=ON -DSCENDERE_SHARED_BOOST=ON && \
make scendere_node -j $(nproc) && \
make scendere_rpc -j $(nproc) && \
make scendere_pow_server -j $(nproc) && \
cd .. && \
echo ${NETWORK} >/etc/scendere-network

FROM ubuntu:18.04

RUN groupadd --gid 1000 scenderecurrency && \
useradd --uid 1000 --gid scenderecurrency --shell /bin/bash --create-home scenderecurrency

COPY --from=0 /tmp/build/scendere_node /usr/bin
COPY --from=0 /tmp/build/scendere_rpc /usr/bin
COPY --from=0 /tmp/build/scendere_pow_server /usr/bin
COPY --from=0 /tmp/src/api/ /usr/bin/api/
COPY --from=0 /etc/scendere-network /etc
COPY docker/node/entry.sh /usr/bin/entry.sh
COPY docker/node/config /usr/share/scendere/config
COPY --from=0 /tmp/boost/lib/* /usr/local/lib/
RUN chmod +x /usr/bin/entry.sh
RUN ln -s /usr/bin/scendere_node /usr/bin/rai_node
RUN ldconfig

WORKDIR /root
USER root

ENV PATH="${PATH}:/usr/bin"
ENTRYPOINT ["/usr/bin/entry.sh"]
CMD ["scendere_node", "daemon", "-l"]
ARG REPOSITORY=scenderecurrency/scendere-node
LABEL org.opencontainers.image.source https://github.com/$REPOSITORY
