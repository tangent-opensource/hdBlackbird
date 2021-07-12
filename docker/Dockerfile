FROM aswf/ci-vfxall:2020

RUN git clone https://github.com/embree/embree.git --depth 1 --branch v3.8.0
RUN cmake embree -G Ninja -DEMBREE_TUTORIALS=OFF -DEMBREE_ISPC_SUPPORT=OFF -DEMBREE_RAY_MASK=ON -DEMBREE_BACKFACE_CULLING=OFF -DEMBREE_FILTER_FUNCTION=ON
RUN cmake --build . --target install

RUN curl -sL https://rpm.nodesource.com/setup_12.x | bash -
RUN sudo yum install -y nodejs
