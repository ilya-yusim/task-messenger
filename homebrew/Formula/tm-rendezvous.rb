class TmRendezvous < Formula
  desc "TaskMessenger Rendezvous - Discovery and monitoring service"
  homepage "https://github.com/ilya-yusim/task-messenger"
  version "0.0.1"
  license "MIT"

  on_macos do
    on_arm do
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-rendezvous-vTest-macos-arm64.tar.gz"
      sha256 ""
    end
  end

  depends_on :macos
  depends_on arch: :arm64

  def install
    # Install binary
    bin.install "bin/tm-rendezvous"

    # Install shared library
    lib.install "lib/libzt.dylib"

    # Install config template and VN identity (server only)
    (etc/"task-messenger").install "config/config-rendezvous.json"
    (etc/"task-messenger").install "config/vn-rendezvous-identity"

    # Install dashboard assets alongside the binary so the rendezvous UI can serve them.
    (bin/"dashboard").install Dir["dashboard/*"] if Dir.exist?("dashboard")

    # Install documentation
    doc.install Dir["doc/*"] if Dir.exist?("doc")
    doc.install "LICENSE" if File.exist?("LICENSE")
  end

  def post_install
    # Create user config directory
    config_dir = "#{Dir.home}/Library/Application Support/TaskMessenger/config/rendezvous"
    FileUtils.mkdir_p(config_dir)

    # Copy config template if doesn't exist
    config_file = "#{config_dir}/config-rendezvous.json"
    unless File.exist?(config_file)
      FileUtils.cp("#{etc}/task-messenger/config-rendezvous.json", config_file)
      ohai "Created config at: #{config_file}"
    end

    # Copy identity directory if doesn't exist
    identity_dir = "#{config_dir}/vn-rendezvous-identity"
    unless Dir.exist?(identity_dir)
      FileUtils.cp_r("#{etc}/task-messenger/vn-rendezvous-identity", identity_dir)
      # Set restrictive permissions on secret file
      FileUtils.chmod(0600, "#{identity_dir}/identity.secret")
      ohai "Created identity at: #{identity_dir}"
    end
  end

  def caveats
    <<~EOS
      TaskMessenger Rendezvous service has been installed!

      Configuration file:
        ~/Library/Application Support/TaskMessenger/config/rendezvous/config-rendezvous.json

      Identity files:
        ~/Library/Application Support/TaskMessenger/config/rendezvous/vn-rendezvous-identity/

      To start the rendezvous service:
        tm-rendezvous -c "~/Library/Application Support/TaskMessenger/config/rendezvous/config-rendezvous.json"

      Or use the full path to the config file.
    EOS
  end

  test do
    system "#{bin}/tm-rendezvous", "--version"
  end
end
