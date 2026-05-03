class TmDispatcher < Formula
  desc "TaskMessenger Dispatcher - Distributed task coordination and dispatch"
  homepage "https://github.com/ilya-yusim/task-messenger"
  version "0.0.3"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-dispatcher-vTest-macos-arm64.tar.gz"
      sha256 ""
    end
  end

  depends_on :macos
  depends_on arch: :arm64

  def install
    # Install binary
    bin.install "bin/tm-dispatcher"

    # Install shared library
    lib.install "lib/libzt.dylib"

    # Install config template
    (etc/"task-messenger").install "config/config-dispatcher.json"

    # Install dashboard assets alongside the binary so the dispatcher can serve them.
    (bin/"dashboard").install Dir["dashboard/*"] if Dir.exist?("dashboard")

    # Install documentation
    doc.install Dir["doc/*"] if Dir.exist?("doc")
    doc.install "LICENSE" if File.exist?("LICENSE")
  end

  def post_install
    # Create user config directory
    config_dir = "#{Dir.home}/Library/Application Support/TaskMessenger/config/dispatcher"
    FileUtils.mkdir_p(config_dir)

    # Copy config template if doesn't exist
    config_file = "#{config_dir}/config-dispatcher.json"
    unless File.exist?(config_file)
      FileUtils.cp("#{etc}/task-messenger/config-dispatcher.json", config_file)
      ohai "Created config at: #{config_file}"
    end
  end

  def caveats
    <<~EOS
      TaskMessenger Dispatcher has been installed!

      Configuration file:
        ~/Library/Application Support/TaskMessenger/config/dispatcher/config-dispatcher.json

      To start the dispatcher:
        tm-dispatcher -c "~/Library/Application Support/TaskMessenger/config/dispatcher/config-dispatcher.json"

      Or use the full path to the config file.
    EOS
  end

  test do
    system "#{bin}/tm-dispatcher", "--version"
  end
end
