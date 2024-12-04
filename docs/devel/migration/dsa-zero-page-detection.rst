=============================
DSA-Based Zero Page Detection
=============================
Intel Data Streaming Accelerator(``DSA``) is introduced in Intel's 4th
generation Xeon server, aka Sapphire Rapids(``SPR``). One of the things
DSA can do is to offload memory comparison workload from CPU to DSA accelerator
hardware.

The main advantages of using DSA to accelerate zero pages detection include

1. Reduces CPU usage in multifd live migration workflow across all use cases.

2. Reduces migration total time in some use cases.


DSA-Based Zero Page Detection Introduction
==========================================

::


  +----------------+       +------------------+
  | MultiFD Thread |       |accel-config tool |
  +-+--------+-----+       +--------+---------+
    |        |                      |
    |        |  Open DSA            | Setup DSA
    |        |  Work Queues         | Resources
    |        |       +-----+-----+  |
    |        +------>|idxd driver|<-+
    |                +-----+-----+
    |                      |
    |                      |
    |                +-----+-----+
    +----------------+DSA Devices|
      Submit jobs    +-----------+
      via enqcmd


DSA Introduction
----------------
Intel Data Streaming Accelerator (DSA) is a high-performance data copy and
transformation accelerator that is integrated in Intel Xeon processors,
targeted for optimizing streaming data movement and transformation operations
common with applications for high-performance storage, networking, persistent
memory, and various data processing applications.

For more ``DSA`` introduction, please refer to `DSA Introduction
<https://www.intel.com/content/www/us/en/products/docs/accelerator-engines/data-streaming-accelerator.html>`_

For ``DSA`` specification, please refer to `DSA Specification
<https://cdrdv2-public.intel.com/671116/341204-intel-data-streaming-accelerator-spec.pdf>`_

For ``DSA`` user guide, please refer to `DSA User Guide
<https://www.intel.com/content/www/us/en/content-details/759709/intel-data-streaming-accelerator-user-guide.html>`_

DSA Device Management
---------------------

The number of ``DSA`` devices will vary depending on the Xeon product model.
On a ``SPR`` server, there can be a maximum of 8 ``DSA`` devices, with up to
4 devices per socket.

By default, all ``DSA`` devices are disabled and need to be configured and
enabled by users manually.

Check the number of devices through the following command

.. code-block:: shell

  #lspci -d 8086:0b25
  6a:01.0 System peripheral: Intel Corporation Device 0b25
  6f:01.0 System peripheral: Intel Corporation Device 0b25
  74:01.0 System peripheral: Intel Corporation Device 0b25
  79:01.0 System peripheral: Intel Corporation Device 0b25
  e7:01.0 System peripheral: Intel Corporation Device 0b25
  ec:01.0 System peripheral: Intel Corporation Device 0b25
  f1:01.0 System peripheral: Intel Corporation Device 0b25
  f6:01.0 System peripheral: Intel Corporation Device 0b25


DSA Device Configuration And Enabling
-------------------------------------

The ``accel-config`` tool is used to enable ``DSA`` devices and configure
``DSA`` hardware resources(work queues and engines). One ``DSA`` device
has 8 work queues and 4 processing engines, multiple engines can be assigned
to a work queue via ``group`` attribute.

For ``accel-config`` installation, please refer to `accel-config installation
<https://github.com/intel/idxd-config>`_

One example of configuring and enabling an ``DSA`` device.

.. code-block:: shell

  #accel-config config-engine dsa0/engine0.0 -g 0
  #accel-config config-engine dsa0/engine0.1 -g 0
  #accel-config config-engine dsa0/engine0.2 -g 0
  #accel-config config-engine dsa0/engine0.3 -g 0
  #accel-config config-wq dsa0/wq0.0 -g 0 -s 128 -p 10 -b 1 -t 128 -m shared -y user -n app1 -d user
  #accel-config enable-device dsa0
  #accel-config enable-wq dsa0/wq0.0

- The ``DSA`` device index is 0, use ``ls -lh /sys/bus/dsa/devices/dsa*``
  command to query the ``DSA`` device index.

- 4 engines and 1 work queue are configured in group 0, so that all zero-page
  detection jobs submitted to this work queue can be processed by all engines
  simultaneously.

- Set work queue attributes including the work mode, work queue size and so on.

- Enable the ``dsa0`` device and work queue ``dsa0/wq0.0``

.. note::

   1. ``DSA`` device driver is Intel Data Accelerator Driver (idxd), it is
      recommended that the minimum version of Linux kernel is 5.18.

   2. Only ``DSA`` shared work queue mode is supported, it needs to add
      ``"intel_iommu=on,sm_on"`` parameter to kernel command line.

For more detailed configuration, please refer to `DSA Configuration Samples
<https://github.com/intel/idxd-config/tree/stable/Documentation/accfg>`_


Performances
============
We use two Intel 4th generation Xeon servers for testing.

::

    Architecture:        x86_64
    CPU(s):              192
    Thread(s) per core:  2
    Core(s) per socket:  48
    Socket(s):           2
    NUMA node(s):        2
    Vendor ID:           GenuineIntel
    CPU family:          6
    Model:               143
    Model name:          Intel(R) Xeon(R) Platinum 8457C
    Stepping:            8
    CPU MHz:             2538.624
    CPU max MHz:         3800.0000
    CPU min MHz:         800.0000

We perform multifd live migration with below setup:

1. VM has 100GB memory.

2. Use the new migration option multifd-set-normal-page-ratio to control the
   total size of the payload sent over the network.

3. Use 8 multifd channels.

4. Use tcp for live migration.

5. Use CPU to perform zero page checking as the baseline.

6. Use one DSA device to offload zero page checking to compare with the baseline.

7. Use "perf sched record" and "perf sched timehist" to analyze CPU usage.


A) Scenario 1: 50% (50GB) normal pages on an 100GB vm
-----------------------------------------------------

::

	CPU usage

	|---------------|---------------|---------------|---------------|
	|		|comm		|runtime(msec)	|totaltime(msec)|
	|---------------|---------------|---------------|---------------|
	|Baseline	|live_migration	|5657.58	|		|
	|		|multifdsend_0	|3931.563	|		|
	|		|multifdsend_1	|4405.273	|		|
	|		|multifdsend_2	|3941.968	|		|
	|		|multifdsend_3	|5032.975	|		|
	|		|multifdsend_4	|4533.865	|		|
	|		|multifdsend_5	|4530.461	|		|
	|		|multifdsend_6	|5171.916	|		|
	|		|multifdsend_7	|4722.769	|41922		|
	|---------------|---------------|---------------|---------------|
	|DSA		|live_migration	|6129.168	|		|
	|		|multifdsend_0	|2954.717	|		|
	|		|multifdsend_1	|2766.359	|		|
	|		|multifdsend_2	|2853.519	|		|
	|		|multifdsend_3	|2740.717	|		|
	|		|multifdsend_4	|2824.169	|		|
	|		|multifdsend_5	|2966.908	|		|
	|		|multifdsend_6	|2611.137	|		|
	|		|multifdsend_7	|3114.732	|		|
	|		|dsa_completion	|3612.564	|32568		|
	|---------------|---------------|---------------|---------------|

Baseline total runtime is calculated by adding up all multifdsend_X
and live_migration threads runtime. DSA offloading total runtime is
calculated by adding up all multifdsend_X, live_migration and
dsa_completion threads runtime. 41922 msec VS 32568 msec runtime and
that is 23% total CPU usage savings.

::

	Latency
	|---------------|---------------|---------------|---------------|---------------|---------------|
	|		|total time	|down time	|throughput	|transferred-ram|total-ram	|
	|---------------|---------------|---------------|---------------|---------------|---------------|
	|Baseline	|10343 ms	|161 ms		|41007.00 mbps	|51583797 kb	|102400520 kb	|
	|---------------|---------------|---------------|---------------|-------------------------------|
	|DSA offload	|9535 ms	|135 ms		|46554.40 mbps	|53947545 kb	|102400520 kb	|
	|---------------|---------------|---------------|---------------|---------------|---------------|

Total time is 8% faster and down time is 16% faster.


B) Scenario 2: 100% (100GB) zero pages on an 100GB vm
-----------------------------------------------------

::

	CPU usage
	|---------------|---------------|---------------|---------------|
	|		|comm		|runtime(msec)	|totaltime(msec)|
	|---------------|---------------|---------------|---------------|
	|Baseline	|live_migration	|4860.718	|		|
	|	 	|multifdsend_0	|748.875	|		|
	|		|multifdsend_1	|898.498	|		|
	|		|multifdsend_2	|787.456	|		|
	|		|multifdsend_3	|764.537	|		|
	|		|multifdsend_4	|785.687	|		|
	|		|multifdsend_5	|756.941	|		|
	|		|multifdsend_6	|774.084	|		|
	|		|multifdsend_7	|782.900	|11154		|
	|---------------|---------------|-------------------------------|
	|DSA offloading	|live_migration	|3846.976	|		|
	|		|multifdsend_0	|191.880	|		|
	|		|multifdsend_1	|166.331	|		|
	|		|multifdsend_2	|168.528	|		|
	|		|multifdsend_3	|197.831	|		|
	|		|multifdsend_4	|169.580	|		|
	|		|multifdsend_5	|167.984	|		|
	|		|multifdsend_6	|198.042	|		|
	|		|multifdsend_7	|170.624	|		|
	|		|dsa_completion	|3428.669	|8700		|
	|---------------|---------------|---------------|---------------|

Baseline total runtime is 11154 msec and DSA offloading total runtime is
8700 msec. That is 22% CPU savings.

::

	Latency
	|--------------------------------------------------------------------------------------------|
	|		|total time	|down time	|throughput	|transferred-ram|total-ram   |
	|---------------|---------------|---------------|---------------|---------------|------------|
	|Baseline	|4867 ms	|20 ms		|1.51 mbps	|565 kb		|102400520 kb|
	|---------------|---------------|---------------|---------------|----------------------------|
	|DSA offload	|3888 ms	|18 ms		|1.89 mbps	|565 kb		|102400520 kb|
	|---------------|---------------|---------------|---------------|---------------|------------|

Total time 20% faster and down time 10% faster.


How To Use DSA In Migration
===========================

The migration parameter ``accel-path`` is used to specify the resource
allocation for DSA. After the user configures
``zero-page-detection=dsa-accel``, one or more DSA work queues need to be
specified for migration.

The following example shows two DSA work queues for zero page detection

.. code-block:: shell

   migrate_set_parameter zero-page-detection=dsa-accel
   migrate_set_parameter accel-path=dsa:/dev/dsa/wq0.0 dsa:/dev/dsa/wq1.0

.. note::

  Accessing DSA resources requires ``sudo`` command or ``root`` privileges
  by default. Administrators can modify the DSA device node ownership
  so that QEMU can use DSA with specified user permissions.

  For example:

  #chown -R qemu /dev/dsa

