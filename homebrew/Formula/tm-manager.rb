class TmManager < Formula
  desc "TaskMessenger Manager - Distributed task coordination and management"
  homepage "https://github.com/ilya-yusim/task-messenger"
  version "Test"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-manager-vTest-macos-arm64.tar.gz"
      sha256 ""
    else
      url "https://github.com/ilya-yusim/task-messenger/releases/download/vTest/tm-manager-vTest-macos-x86_64.tar.gz"
      sha256 "PLACEHOLDER_X86_64_SHA256"
    end
  end

  depends_on :macos

  def install
    # Install binary
    bin.install "bin/tm-manager"
    
    # Install shared library
    lib.install "lib/libzt.dylib"
    
    # Install config templates
    (etc/"task-messenger").install "config/config-manager.json"
    (etc/"task-messenger").install "config/vn-manager-identity"
    
    # Install documentation
    doc.install Dir["doc/*"] if Dir.exist?("doc")
    doc.install "LICENSE" if File.exist?("LICENSE")
  end

  def post_install
    # Create user config directory
    config_dir = "#{Dir.home}/Library/Application Support/TaskMessenger/config/manager"
    FileUtils.mkdir_p(config_dir)
    
    # Copy config template if doesn't exist
    config_file = "#{config_dir}/config-manager.json"
    unless File.exist?(config_file)
      FileUtils.cp("#{etc}/task-messenger/config-manager.json", config_file)
      ohai "Created config at: #{config_file}"
    end
    
    # Copy identity directory if doesn't exist
    identity_dir = "#{config_dir}/vn-manager-identity"
    unless Dir.exist?(identity_dir)
      FileUtils.cp_r("#{etc}/task-messenger/vn-manager-identity", identity_dir)
      # Set restrictive permissions on secret file
      FileUtils.chmod(0600, "#{identity_dir}/identity.secret")
      ohai "Created identity at: #{identity_dir}"
    end
  end

  def caveats
    <<~EOS
      TaskMessenger Manager has been installed!
      
      Configuration file:
        ~/Library/Application Support/TaskMessenger/config/manager/config-manager.json
      
      Identity files:
        ~/Library/Application Support/TaskMessenger/config/manager/vn-manager-identity/
      
      To start the manager:
        tm-manager -c "~/Library/Application Support/TaskMessenger/config/manager/config-manager.json"
      
      Or use the full path to the config file.
    EOS
  end

  test do
    system "#{bin}/tm-manager", "--version"
  end
end
